// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_099_toml_live.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-14's live cell (tasks.md T095), FR-017:
// a TOML bundle naming FIX50SP2.xml with extra_paths FIXT11.xml and FIX Latest, a real Engine on a
// worker thread, and a raw TCP peer. The session reaches Active; `1128=10` is routed, so 3115, which
// only FIX Latest declares, is delivered; the same frame with no 1128 draws 373=2, 371=3115. Its own
// executable under [const §VII.8], because it runs threads and a socket.
//
// After Engine::start() every check is non-fatal until stop() has completed, because ~Engine's
// precondition aborts on a fatal return in between.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/fixt_raw_peer.hpp"
#include "support/temp_dir.hpp"

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake"
#endif

namespace {

namespace dd = fixpp::dict;
using namespace std::chrono_literals;
using fixpp::test_support::fix_utc_now;
using fixpp::test_support::fixt_frame;
using fixpp::test_support::FixtRawPeer;
using fixpp::test_support::frame_field;

constexpr auto kStep = 15s;

std::string vendored(std::string_view relative) {
    return (std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative).generic_string();
}

std::string bundle_text(std::uint16_t port) {
    return "[clock]\nkind = \"system\"\n\n[store]\nkind = \"memory\"\n\n[dictionary]\n"
           "kind = \"path\"\npath = \"" +
           vendored("FIX50SP2.xml") + "\"\nextra_paths = [\"" + vendored("FIXT11.xml") + "\", \"" +
           vendored("orchestra/OrchestraFIXLatest.xml") +
           "\"]\n\n[[session]]\n"
           "sender_comp_id = \"ISLD\"\n"
           "target_comp_id = \"TW\"\n"
           "begin_string = \"FIXT.1.1\"\n"
           "default_appl_ver_id = \"9\"\n"
           "role = \"initiator\"\n"
           "heartbeat_interval = \"30s\"\n"
           "validate_inbound_messages = true\n"
           "\n[session.transport]\nkind = \"plaintext\"\nhost = \"127.0.0.1\"\nport = " +
           std::to_string(port) +
           "\n\n[session.security_profile]\nkind = \"insecure_plain_tcp\"\n";
}

class RecordingApp final : public fixpp::session::Application {
public:
    using View = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
    std::atomic<bool> active{false};

    void onLogon(const fixpp::session::SessionId&) override { active.store(true); }
    fixpp::core::expected_t<void> fromApp(const View& v, const fixpp::session::SessionId&) override {
        const std::scoped_lock lock{mu_};
        ++delivered_;
        versions_.push_back(v.resolved_application_version());
        auto f = v.get(3115);
        last_3115_ = f ? std::string{f->as_string()} : std::string{};
        return {};
    }

    struct Seen {
        int delivered = 0;
        std::vector<dd::application_version> versions;
        std::string last_3115;
    };
    [[nodiscard]] Seen seen() const {
        const std::scoped_lock lock{mu_};
        return Seen{delivered_, versions_, last_3115_};
    }

private:
    mutable std::mutex mu_;
    int delivered_ = 0;
    std::vector<dd::application_version> versions_;
    std::string last_3115_;
};

std::string header(unsigned seq) {
    return "34=" + std::to_string(seq) + "\x01" + "49=TW\x01" + "52=" + fix_utc_now() + "\x01" +
           "56=ISLD\x01";
}

const std::string kNosWith3115 =
    "11=C\x01"
    "21=1\x01"
    "55=X\x01"
    "54=1\x01"
    "60=20240101-00:00:00\x01"
    "40=1\x01"
    "38=1\x01"
    "3115=1.5\x01";

TEST(W14Live, ReachesActive_1128Routes) {
    FixtRawPeer peer;
    const std::filesystem::path dir = fixpp::test_support::unique_temp_dir("w14live");
    {
        std::ofstream(dir / "bundle.toml") << bundle_text(peer.port());
    }
    asio::io_context ioc;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ioc.get_executor();
    auto loaded = fixpp::config::load_toml_config(dir / "bundle.toml", opts);
    ASSERT_TRUE(loaded.has_value());
    fixpp::test_support::try_remove_temp_dir(dir);

    auto app = std::make_shared<RecordingApp>();
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = loaded->engine.clock;
    ec.default_store_factory = loaded->engine.default_store_factory;
    ec.default_transport_factory = loaded->engine.default_transport_factory;
    ec.dictionaries = loaded->engine.dictionaries;
    ec.application = app;
    fixpp::session::Engine engine{ioc.get_executor(), std::move(ec)};
    const auto registered = engine.register_session(loaded->sessions.at(0).config);
    ASSERT_TRUE(registered.has_value()) << "register_session returned "
                                        << static_cast<int>(registered.error());
    const auto started = engine.start();
    ASSERT_TRUE(started.has_value());

    auto guard = asio::make_work_guard(ioc);
    std::thread worker{[&ioc] { ioc.run(); }};

    std::string reject;
    bool routed_delivered = false;
    if (peer.accept(kStep)) {
        const auto logon = peer.read_msg_type("A", kStep);
        EXPECT_TRUE(logon.has_value()) << "no Logon from the initiator";
        if (logon) {
            EXPECT_EQ(frame_field(*logon, 1137), "9");
            EXPECT_TRUE(peer.write(fixt_frame("35=A\x01" + header(1) + "98=0\x01" +
                                              "108=30\x01" + "1137=9\x01")));
            const auto end = std::chrono::steady_clock::now() + kStep;
            while (!app->active.load() && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(2ms);
            }
            EXPECT_TRUE(app->active.load()) << "the session did not reach Active";

            EXPECT_TRUE(peer.write(fixt_frame("35=D\x01" + std::string{"1128=10\x01"} + header(2) +
                                              kNosWith3115)));
            // The frame after it is the fence: its Reject is written after the first was handled.
            EXPECT_TRUE(peer.write(fixt_frame("35=D\x01" + header(3) + kNosWith3115)));
            const auto r = peer.read_msg_type("3", kStep);
            EXPECT_TRUE(r.has_value()) << "no Reject for the frame with no 1128";
            reject = r.value_or("");
            routed_delivered = true;
        }
    } else {
        ADD_FAILURE() << "the initiator did not connect";
    }

    auto stopped = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    EXPECT_EQ(stopped.wait_for(30s), std::future_status::ready);
    guard.reset();
    peer.close();
    worker.join();

    EXPECT_TRUE(routed_delivered);
    const RecordingApp::Seen seen = app->seen();
    EXPECT_EQ(seen.delivered, 1);
    ASSERT_EQ(seen.versions.size(), 1U);
    EXPECT_EQ(seen.versions[0], dd::application_version::vlatest);
    EXPECT_EQ(seen.last_3115, "1.5");
    EXPECT_EQ(frame_field(reject, 373), "2");
    EXPECT_EQ(frame_field(reject, 371), "3115");
    EXPECT_EQ(frame_field(reject, 372), "D");
    EXPECT_EQ(frame_field(reject, 45), "3");
}

}  // namespace
