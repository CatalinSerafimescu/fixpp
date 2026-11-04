// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_route_set_cost.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-15 (tasks.md T033), NFR-003, SC-007:
// an Engine with N FIXT sessions on M distinct route tables walks M times, in start(), and no more
// at any open; a session holds no copy of a table. Its own executable under [const §VII.8]: a
// global operator new counter, and an arm that runs threads for TSan.
//
// M is computed here from the distinct Dictionary*, plus one per (S with a header pair, other
// dictionary). The non-FIXT twin's delta and one copy's bytes are measured here too. Its mutants
// are M-8, M-8b, M-41 and M-84.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "dictionary_internal.hpp"  // as_table_view_call_count, the walk counter
#include "session/fixt_route_set.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/engine_test_access.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"
#include "test_099_fixt_support.hpp"

// ASan, TSan and MSan own operator new. Under them the heap arm skips; the rest runs.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW
namespace {
std::atomic<bool> g_counting{false};
std::atomic<std::uint64_t> g_bytes{0};
}  // namespace

// GCC's -Wmismatched-new-delete pairs std::free with the STANDARD operator new; these replace it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_bytes.fetch_add(size, std::memory_order_relaxed);
    }
    void* p = std::malloc(size != 0U ? size : 1U);
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

namespace fixpp::session::test099 {
namespace {

namespace rig = fixpp::test_support::plain_rig;
using AV = dd::application_version;
using fixpp::dict::detail::as_table_view_call_count;
using fixpp::dict::detail::reset_as_table_view_call_count;

struct QuietApp final : Application {};

// FIX44 with a body field only this overlay declares.
const DictPtr& overlay_s() {
    static const DictPtr d =
        overlay_on("FIX44.xml",
                   "<fix type='FIX' major='4' minor='4' servicepack='0'><messages>"
                   "<message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                   "<field name='VenueCost' required='N' /></message></messages><fields>"
                   "<field number='5020' name='VenueCost' type='STRING' /></fields></fix>");
    return d;
}

// FIX44 with a Length+Data pair in its header: the one S here that declares a header pair.
const DictPtr& header_pair_s() {
    static const DictPtr d = overlay_on("FIX44.xml",
                                        "<fix type='FIX' major='4' minor='4' servicepack='0'>"
                                        "<header>" +
                                            kPairMembers + "</header>" + kPairFields + "</fix>");
    return d;
}

struct Member {
    std::string sender;
    DictPtr s;
    session_role role = session_role::acceptor;
    bool validate = true;
    std::string begin_string = kFT;
    std::optional<AV> default_appl;
    bool declares_header_pair = false;
};

// The plaintext session an Engine registers for `m`. Its reconnect endpoint is the bind endpoint
// on an acceptor, and the peer's listener on an initiator.
SessionConfig config_of(const Member& m, std::uint16_t port) {
    SessionConfig c;
    c.sender_comp_id = m.sender;
    c.target_comp_id = "TW" + m.sender;
    c.begin_string = m.begin_string;
    c.role = m.role;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    c.security_profile = SecurityProfile{SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    c.dictionary = m.s;
    c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
    c.heartbeat_interval = std::chrono::seconds{30};
    c.logout_disconnect_timeout_ms = 500;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
    if (m.default_appl) {
        c.default_appl_ver_id = *m.default_appl;
    }
    c.validate_inbound_messages = m.validate;
    c.transport_send = [](std::span<const std::byte>) {};
    return c;
}

// An Engine and a raw peer per member, on one io_context the test thread pumps. An initiator
// connects to a listener the peer accepts on; an acceptor opens once its peer has connected and
// sent a first frame, so `open_all` is what makes every open happen.
class Scene {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::vector<Member> members;
    std::vector<std::unique_ptr<rig::Peer>> peers;
    std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> listeners;
    std::unique_ptr<Engine> engine;
    std::vector<bool> opened;
    std::vector<std::uint64_t> walks_at_open;  // the walk counter when each session first opened
    std::vector<const detail::fixt_route_set*> set_at_open;  // the set each session held

    Scene(std::vector<DictPtr> engine_dictionaries, std::vector<Member> group)
        : members(std::move(group)) {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        fixpp::core::EngineConfig ec;
        ec.executor = ioc.get_executor();
        ec.clock = clock;
        ec.application = std::make_shared<QuietApp>();
        ec.dictionaries = std::move(engine_dictionaries);
        engine = std::make_unique<Engine>(ioc.get_executor(), std::move(ec));
        for (const Member& m : members) {
            peers.push_back(std::make_unique<rig::Peer>(ioc));
            std::uint16_t port = 0;
            if (m.role == session_role::initiator) {
                listeners.push_back(std::make_unique<asio::ip::tcp::acceptor>(
                    ioc, asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}));
                port = listeners.back()->local_endpoint().port();
                rig::Peer* peer = peers.back().get();
                listeners.back()->async_accept(peer->sock, [peer](std::error_code const& ec2) {
                    if (!ec2) {
                        peer->start_reading();
                    }
                });
            }
            registered_ok =
                engine->register_session(config_of(m, port)).has_value() && registered_ok;
        }
        opened.assign(members.size(), false);
        walks_at_open.assign(members.size(), 0);
        set_at_open.assign(members.size(), nullptr);
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;
    ~Scene() { stop(); }

    bool registered_ok = true;

    [[nodiscard]] SessionId id_of(const Member& m) const {
        return SessionId::from_config(config_of(m, 0));
    }

    [[nodiscard]] std::string logon_of(const Member& m) const {
        std::array<char, 32> buf{};
        const auto st = fixpp::core::utc_time_to_fix_string(
            clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        const std::string stamp = st ? std::string{st->data(), st->size()} : std::string{};
        return rig::message(m.begin_string, "A", 1, "TW" + m.sender, m.sender, stamp,
                            m.begin_string == kFT ? "98=0\x01"
                                                    "108=30\x01"
                                                    "1137=6\x01"
                                                  : "98=0\x01"
                                                    "108=30\x01");
    }

    // Connects each acceptor's peer and sends its first frame, and pumps until every session has
    // opened. An opened session stays counted even if its Logon then ends it. True if all opened.
    [[nodiscard]] bool open_all() {
        std::vector<bool> connected(members.size(), false);
        const auto all = [&] {
            bool done = true;
            for (std::size_t i = 0; i < members.size(); ++i) {
                const Member& m = members[i];
                if (m.role == session_role::acceptor && !connected[i]) {
                    const auto port = engine->acceptor_bound_endpoint(id_of(m)).port;
                    if (port != 0) {
                        connected[i] = true;
                        rig::Peer* peer = peers[i].get();
                        peer->sock.async_connect(
                            asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                            [peer, frame = logon_of(m)](std::error_code const& ec) {
                                if (!ec) {
                                    peer->start_reading();
                                    peer->send(frame);
                                }
                            });
                    }
                }
                if (!opened[i]) {
                    if (const auto session = engine->lookup(id_of(m))) {
                        opened[i] = true;
                        walks_at_open[i] = as_table_view_call_count();
                        set_at_open[i] = session_test_access::routes(*session).get();
                    }
                }
                done = done && opened[i];
            }
            return done;
        };
        return fixpp::test_support::pump_until(ioc, all, std::chrono::seconds{60},
                                               std::chrono::milliseconds{1}, nullptr);
    }

    void stop() {
        if (engine && !stopped_) {
            stopped_ = true;
            auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
            if (!fixpp::test_support::pump_until_ready(ioc, fut, std::chrono::seconds{60},
                                                       "W15 Scene::stop")) {
                std::abort();  // ~Engine would abort on its precondition anyway
            }
            (void)fut.get();
        }
    }

private:
    bool stopped_ = false;
};

// N sessions, both roles: two T sessions on one S, two on one overlay S, one with a header pair,
// and one non-validating, whose default has a slot, so it opens.
std::vector<DictPtr> fixt_engine_dictionaries() { return {fixt11(), fix44(), fix42()}; }

std::vector<Member> fixt_members() {
    return {
        Member{"T1", fixt11(), session_role::initiator, true, kFT, AV::v44},
        Member{"T2", fixt11(), session_role::acceptor, true, kFT, AV::v44},
        Member{"OV1", overlay_s(), session_role::initiator, true, kFT, AV::v44},
        Member{"OV2", overlay_s(), session_role::acceptor, true, kFT, AV::v44},
        Member{"PR1", header_pair_s(), session_role::acceptor, true, kFT, AV::v44, true},
        Member{"NV1", fix42(), session_role::initiator, false, kFT, AV::v42},
    };
}

// The non-FIXT twin of fixt_members(): the same roles and validation, one minimal S.
std::vector<Member> non_fixt_members(const std::string& begin_string, const DictPtr& s) {
    std::vector<Member> out;
    for (const Member& m : fixt_members()) {
        out.push_back(Member{m.sender, s, m.role, m.validate, begin_string, std::nullopt});
    }
    return out;
}

// One entry per distinct Dictionary*, plus one per (S with a header pair, other dictionary).
std::uint64_t expected_walks(const std::vector<DictPtr>& engine_dictionaries,
                             const std::vector<Member>& members) {
    std::set<const dd::Dictionary*> distinct;
    std::set<const dd::Dictionary*> pair_sources;
    for (const DictPtr& d : engine_dictionaries) {
        distinct.insert(d.get());
    }
    for (const Member& m : members) {
        distinct.insert(m.s.get());
        if (m.declares_header_pair) {
            pair_sources.insert(m.s.get());
        }
    }
    return distinct.size() + (pair_sources.size() * (distinct.size() - 1));
}

TEST(W15FixtPass, WalksMAfterStartAndStillMAfterEveryOpen) {
    const auto engine_dictionaries = fixt_engine_dictionaries();
    const auto members = fixt_members();
    const std::uint64_t m = expected_walks(engine_dictionaries, members);
    ASSERT_EQ(m, 9U) << "five distinct dictionaries, and four more for the header pair";

    std::uint64_t after_start = 0;
    std::uint64_t after_opens = 0;
    bool started = false;
    bool all_opened = false;
    std::vector<std::uint64_t> at_open;
    std::vector<const detail::fixt_route_set*> set_at_open;
    const detail::fixt_route_set* engine_set = nullptr;
    {
        Scene scene{engine_dictionaries, members};
        EXPECT_TRUE(scene.registered_ok);
        reset_as_table_view_call_count();
        started = scene.engine->start().has_value();
        after_start = as_table_view_call_count();
        const auto held = engine_test_access::routes(*scene.engine);
        engine_set = held.get();
        all_opened = scene.open_all();
        after_opens = as_table_view_call_count();
        at_open = scene.walks_at_open;
        set_at_open = scene.set_at_open;
        scene.stop();
    }
    EXPECT_TRUE(started);
    EXPECT_TRUE(all_opened) << "not every session opened, so no open was counted";
    EXPECT_EQ(after_start, m) << "start() walks each route table once";
    EXPECT_EQ(after_opens, m) << "an open walks nothing";
    EXPECT_NE(engine_set, nullptr);
    for (std::size_t i = 0; i < at_open.size(); ++i) {
        EXPECT_EQ(at_open[i], m) << members[i].sender;
        EXPECT_EQ(set_at_open[i], engine_set) << members[i].sender << " holds another set";
    }
}

// On an engine with a FIXT session, a non-FIXT session takes no part: it walks for itself, as
// before this feature, and holds no set. So does a FIXT.1.1 entry with no default, which open()
// refuses: its S is not walked, and the log's cell in W-8 reads what that entry does.
TEST(W15Mixed, ANonFixtSessionAndANoDefaultEntryTakeNoPart) {
    const std::vector<DictPtr> dictionaries{fixt11(), fix44()};
    const std::vector<Member> fixt{
        Member{"T1", fixt11(), session_role::acceptor, true, kFT, AV::v44}};
    const std::uint64_t m = expected_walks(dictionaries, fixt);
    std::vector<Member> members = fixt;
    members.push_back(
        Member{"F44", fix44(), session_role::acceptor, true, "FIX.4.4", std::nullopt});
    std::uint64_t after_start = 0;
    std::uint64_t after_opens = 0;
    bool all_opened = false;
    bool no_default_registered = false;
    std::vector<const detail::fixt_route_set*> set_at_open;
    {
        Scene scene{dictionaries, members};
        EXPECT_TRUE(scene.registered_ok);
        Member no_default{"ND", overlay_s(), session_role::initiator, false, kFT, std::nullopt};
        no_default_registered =
            scene.engine->register_session(config_of(no_default, 1)).has_value();
        reset_as_table_view_call_count();
        EXPECT_TRUE(scene.engine->start().has_value());
        after_start = as_table_view_call_count();
        all_opened = scene.open_all();
        after_opens = as_table_view_call_count();
        set_at_open = scene.set_at_open;
        scene.stop();
    }
    EXPECT_TRUE(no_default_registered);
    EXPECT_TRUE(all_opened);
    EXPECT_EQ(after_start, m) << "S of the entry with no default must not be walked";
    EXPECT_EQ(after_opens, m + 1) << "the FIX.4.4 session walks once, for itself";
    EXPECT_NE(set_at_open[0], nullptr);
    EXPECT_EQ(set_at_open[1], nullptr) << "a non-FIXT session holds no set";
}

TEST(W15NonFixtPass, OneWalkPerOpen) {
    const DictPtr s = fixpp::test_support::make_minimal_dictionary();
    const auto members = non_fixt_members("FIX.4.2", s);
    std::uint64_t after_start = 1;
    std::uint64_t after_opens = 0;
    bool started = false;
    bool all_opened = false;
    {
        Scene scene{{}, members};
        EXPECT_TRUE(scene.registered_ok);
        reset_as_table_view_call_count();
        started = scene.engine->start().has_value();
        after_start = as_table_view_call_count();
        all_opened = scene.open_all();
        after_opens = as_table_view_call_count();
        EXPECT_EQ(engine_test_access::routes(*scene.engine), nullptr);
        EXPECT_EQ(std::ranges::count(scene.set_at_open, nullptr),
                  static_cast<std::ptrdiff_t>(members.size()));
        scene.stop();
    }
    EXPECT_TRUE(started);
    EXPECT_TRUE(all_opened);
    EXPECT_EQ(after_start, 0U) << "a non-FIXT engine builds no set";
    EXPECT_EQ(after_opens, members.size()) << "one walk per open, as before this feature";
}

// Green on `c0a828ef`, stated: an engine with no FIXT session takes no part. It is what the set's
// condition in start() has to keep.
TEST(W15AllFix44Engine, NoSet) {
    const auto members = non_fixt_members("FIX.4.4", fix44());
    std::uint64_t after_start = 1;
    std::uint64_t after_opens = 0;
    bool started = false;
    bool all_opened = false;
    {
        Scene scene{{fix44()}, members};
        EXPECT_TRUE(scene.registered_ok);
        reset_as_table_view_call_count();
        started = scene.engine->start().has_value();
        after_start = as_table_view_call_count();
        all_opened = scene.open_all();
        after_opens = as_table_view_call_count();
        EXPECT_EQ(engine_test_access::routes(*scene.engine), nullptr);
        EXPECT_EQ(std::ranges::count(scene.set_at_open, nullptr),
                  static_cast<std::ptrdiff_t>(members.size()));
        scene.stop();
    }
    EXPECT_TRUE(started);
    EXPECT_TRUE(all_opened);
    EXPECT_EQ(after_start, 0U);
    EXPECT_EQ(after_opens, members.size());
}

#if !FIXPP_SANITIZER_REPLACES_NEW
// Cumulative bytes through operator new around the opens, so an upper bound of what a session
// keeps. Each pass counts its peers' writes and the same transports, which cancel between them.
std::uint64_t bytes_around_the_opens(Scene& scene, bool* opened) {
    (void)scene.engine->start();
    g_bytes.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    *opened = scene.open_all();
    g_counting.store(false, std::memory_order_relaxed);
    const std::uint64_t bytes = g_bytes.load(std::memory_order_relaxed);
    scene.stop();
    return bytes;
}

// One copy of a route table of `d`: what a session would pay to hold its own.
std::uint64_t copy_bytes(const dd::Dictionary& d) {
    const dd::table_view table = d.as_table_view(dd::fixt_framing);
    g_bytes.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    {
        const dd::table_view copy = table;
        (void)copy;
    }
    g_counting.store(false, std::memory_order_relaxed);
    return g_bytes.load(std::memory_order_relaxed);
}
#endif

TEST(W15Heap, AnAddedFixtSessionAddsLessThanOneCopyOfTheSmallestDictionary) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "this sanitizer owns operator new";
#else
    const auto fixt = fixt_members();
    const DictPtr minimal = fixpp::test_support::make_minimal_dictionary();
    const auto twin = non_fixt_members("FIX.4.2", minimal);

    // Costs a first scene pays once, for the process, go here and are not charged to either pass.
    {
        Scene warm{{}, twin};
        bool warm_opened = false;
        (void)bytes_around_the_opens(warm, &warm_opened);
    }
    bool twin_opened = false;
    std::uint64_t twin_bytes = 0;
    {
        Scene scene{{}, twin};
        twin_bytes = bytes_around_the_opens(scene, &twin_opened);
    }
    bool fixt_opened = false;
    std::uint64_t fixt_bytes = 0;
    {
        Scene scene{fixt_engine_dictionaries(), fixt};
        fixt_bytes = bytes_around_the_opens(scene, &fixt_opened);
    }
    const std::uint64_t smallest = std::min(copy_bytes(*fix42()), copy_bytes(*fix44()));

    EXPECT_TRUE(twin_opened);
    EXPECT_TRUE(fixt_opened);
    EXPECT_GT(twin_bytes, 0U) << "the counter must report the twin's opens";
    EXPECT_GT(fixt_bytes, 0U) << "the counter must report the opens";
    EXPECT_GT(smallest, 0U) << "the counter must report one copy";
    const auto per_fixt = static_cast<std::int64_t>(fixt_bytes / fixt.size());
    const auto per_twin = static_cast<std::int64_t>(twin_bytes / twin.size());
    EXPECT_LT(per_fixt - per_twin, static_cast<std::int64_t>(smallest))
        << "per FIXT session " << per_fixt << ", per non-FIXT session " << per_twin
        << ", one copy of the smallest routed dictionary " << smallest;
#endif
}

// ── The arm TSan judges: worker threads above one ───────────────────────────────────────────

constexpr int kThreadSessions = 4;
constexpr int kThreadFrames = 16;

// Reads from `sock` until `needle` arrives or the budget ends. On a thread of its own, so it may
// block; the session's strand is what it waits on.
bool read_until(asio::ip::tcp::socket& sock, std::string& seen, const std::string& needle) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    std::array<char, 4096> buf{};
    while (seen.find(needle) == std::string::npos) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::error_code ec;
        const std::size_t n = sock.read_some(asio::buffer(buf), ec);
        if (ec == asio::error::would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            continue;
        }
        if (ec) {
            return false;
        }
        seen.append(buf.data(), n);
    }
    return true;
}

// N threads validating through one entry's validator, each with its own scratch: what several
// strands do at once. A frame the table accepts, so that a thread also shows it ran the validator.
void validate_on_threads(const detail::fixt_route_set::entry& route, const std::string& frame,
                         std::atomic<int>& passes, int threads, int frames_each) {
    std::vector<std::thread> validating;
    for (int t = 0; t < threads; ++t) {
        validating.emplace_back([&route, &frame, &passes, frames_each] {
            const auto bytes = std::as_bytes(std::span{frame.data(), frame.size()});
            for (int i = 0; i < frames_each; ++i) {
                std::pmr::monotonic_buffer_resource parse_mr;
                std::pmr::monotonic_buffer_resource carry_mr;
                std::pmr::monotonic_buffer_resource scratch_mr;
                fixpp::wire::pmr_carry_buffer carry{512, &carry_mr};
                fixpp::wire::Framer framer;
                std::array<fixpp::wire::frame_view, 1> out{};
                auto fed = framer.feed(bytes, carry, std::span<fixpp::wire::frame_view>{out});
                if (!fed || fed->empty()) {
                    continue;
                }
                fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{*route.owner};
                auto parsed = parser.parse((*fed)[0], &parse_mr);
                if (!parsed) {
                    continue;
                }
                std::uint16_t ref_tag = 0;
                if (route.validator->validate(*parsed, &scratch_mr, &ref_tag).has_value()) {
                    passes.fetch_add(1);
                }
            }
        });
    }
    for (auto& t : validating) {
        t.join();
    }
}

// Every session on a worker thread of its own strand, its gate running on whichever thread the
// frame lands, all of them over one Dictionary*. A peer's Heartbeat answer to the last TestRequest
// says that every frame ahead of it passed through the gate.
TEST(W15Threads, WorkerThreadsAboveOne) {
    asio::io_context ioc;
    auto clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    ec.application = std::make_shared<QuietApp>();
    ec.dictionaries = {fixt11(), fix44()};
    const std::uint64_t m = expected_walks(ec.dictionaries, {});
    Engine engine{ioc.get_executor(), std::move(ec)};

    std::vector<Member> members;
    for (int i = 0; i < kThreadSessions; ++i) {
        members.push_back(
            Member{"S" + std::to_string(i), fixt11(), session_role::acceptor, true, kFT, AV::v44});
    }
    bool registered = true;
    for (const Member& mm : members) {
        registered = engine.register_session(config_of(mm, 0)).has_value() && registered;
    }

    reset_as_table_view_call_count();
    const bool started = engine.start().has_value();
    const std::uint64_t after_start = as_table_view_call_count();

    auto guard = asio::make_work_guard(ioc);
    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back([&ioc] { ioc.run(); });
    }

    // Everything between start() and the stop below stays non-fatal: ~Engine aborts unless stopped.
    std::vector<std::thread> peers;
    std::atomic<int> answered{0};
    for (const Member& mm : members) {
        peers.emplace_back([&, mm] {
            const SessionId id = SessionId::from_config(config_of(mm, 0));
            std::uint16_t port = 0;
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{60};
            while (port == 0 && std::chrono::steady_clock::now() < end) {
                port = engine.acceptor_bound_endpoint(id).port;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            asio::io_context peer_ioc;
            asio::ip::tcp::socket sock{peer_ioc};
            std::error_code err;
            (void)sock.connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                               err);
            if (err) {
                return;
            }
            sock.non_blocking(true, err);
            auto stamp = [&] {
                std::array<char, 32> buf{};
                const auto st = fixpp::core::utc_time_to_fix_string(
                    clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
                return st ? std::string{st->data(), st->size()} : std::string{};
            };
            auto send_all = [&](const std::string& frame) {
                std::error_code werr;
                std::size_t off = 0;
                while (off < frame.size() && !werr) {
                    const std::size_t n =
                        sock.write_some(asio::buffer(frame.data() + off, frame.size() - off), werr);
                    off += n;
                    if (werr == asio::error::would_block) {
                        werr.clear();
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                }
            };
            const std::string peer_id = "TW" + mm.sender;
            std::uint32_t seq = 1;
            send_all(rig::message(kFT, "A", seq++, peer_id, mm.sender, stamp(),
                                  "98=0\x01"
                                  "108=30\x01"
                                  "1137=6\x01"));
            std::string seen;
            for (int f = 0; f < kThreadFrames; ++f) {
                send_all(rig::message(kFT, "D", seq++, peer_id, mm.sender, stamp(), kNos));
            }
            send_all(rig::message(kFT, "1", seq++, peer_id, mm.sender, stamp(), "112=last\x01"));
            if (read_until(sock, seen,
                           "\x01"
                           "112=last\x01")) {
                answered.fetch_add(1);
            }
        });
    }
    for (auto& t : peers) {
        t.join();
    }
    const std::uint64_t after_traffic = as_table_view_call_count();

    // Every session holds the Engine's one set, so each sees the same entry per slot.
    const auto held = engine_test_access::routes(engine);
    int sessions_on_the_set = 0;
    for (const Member& mm : members) {
        if (const auto s = engine.lookup(SessionId::from_config(config_of(mm, 0)))) {
            sessions_on_the_set += session_test_access::routes(*s) == held ? 1 : 0;
        }
    }
    constexpr int kValidatingThreads = 4;
    constexpr int kFramesEach = 200;
    std::atomic<int> passes{0};
    if (held && held->find(fixt11().get(), nullptr) != nullptr) {
        validate_on_threads(*held->find(fixt11().get(), nullptr),
                            rig::message(kFT, "0", 1, "TW", "ISLD", "20240101-00:00:00.000"),
                            passes, kValidatingThreads, kFramesEach);
    }

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    const bool stopped = stop_fut.wait_for(std::chrono::seconds{60}) == std::future_status::ready;
    if (!stopped) {
        std::abort();  // ~Engine would abort on its precondition anyway
    }
    (void)stop_fut.get();
    guard.reset();
    for (auto& t : workers) {
        t.join();
    }

    EXPECT_TRUE(registered);
    EXPECT_TRUE(started);
    EXPECT_EQ(answered.load(), kThreadSessions) << "a session did not answer its last TestRequest";
    EXPECT_EQ(after_start, m);
    EXPECT_EQ(after_traffic, m) << "an open on a worker thread walked";
    EXPECT_EQ(sessions_on_the_set, kThreadSessions) << "a session holds another set";
    EXPECT_EQ(passes.load(), kValidatingThreads * kFramesEach)
        << "the threads must validate, or this arm reports no race for that reason";
}

}  // namespace
}  // namespace fixpp::session::test099
