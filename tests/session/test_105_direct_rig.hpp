// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_direct_rig.hpp — 105-transport-liveness, User Story 1 part B (tasks.md
// T046, T048 to T051): a Session driven with no Engine, whose connection is a
// BlockingWriteTransport (tests/support/blocking_write_transport.hpp) and whose next write can be
// held until the cell releases it. It is the rig for the write limit (contract C-2.7) and for the
// direct-Session forms of the wait.
//
// Both roles: the acceptor's transport is attached with attach_accepted_transport, as
// test_104_reset_on_logout.cpp's open_live_acceptor attaches it; the initiator's is made by the
// same factory at drive_reconnect(). Frames reach the session through resend_gap::Scene::feed or
// feed_spawn (a frame whose processing blocks in a held write). The peer-visible close is the
// transport's close (the shared ScriptedStream's closed flag); what the peer received is the
// transport's byte record, of which only completed writes count.
//
// CONDITIONS this header keeps, checked by reading: it names no production symbol 105 adds, so
// it compiles on the base; no helper reports with ASSERT_*.
#pragma once

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/extract_tag.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_executor_of.hpp"

namespace fixpp::test_support::t105 {

namespace bwt = fixpp::session::test;
namespace rgs = fixpp::test_support::resend_gap;

// Counts the lifecycle callbacks; runs a hook from toAdmin with the frame's MsgType(35), before
// the frame's number and store call. Optionally refuses one administrative MsgType in fromAdmin
// and every application message in fromApp (app_do_not_send); both off by default.
class DirectApp final : public rgs::GapApp {
public:
    int logouts = 0;
    int from_admin_logouts = 0;
    std::function<void(std::string_view)> to_admin_hook;
    std::string refuse_admin_type;  // empty: fromAdmin accepts every administrative message
    bool refuse_app = false;

    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        rgs::GapApp::toAdmin(msg, id);
        if (to_admin_hook) to_admin_hook(msg.msg_type());
    }
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        if (msg.msg_type() == "5") ++from_admin_logouts;
        if (!refuse_admin_type.empty() && msg.msg_type() == refuse_admin_type) {
            return std::unexpected(fixpp::core::error::app_do_not_send);
        }
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& id) override {
        if (refuse_app) return std::unexpected(fixpp::core::error::app_do_not_send);
        return rgs::GapApp::fromApp(msg, id);
    }
};

class DirectRig {
public:
    fixpp::session::session_role role;
    std::shared_ptr<DirectApp> app = std::make_shared<DirectApp>();
    rgs::Scene s;
    // Declared after the Scene, so destroyed first: the held write's timer holds an executor of
    // the Scene's io_context.
    std::shared_ptr<bwt::blocking_write_state> bw = std::make_shared<bwt::blocking_write_state>();
    std::shared_ptr<bwt::ScriptedStream> stream = std::make_shared<bwt::ScriptedStream>();

    // The session's config is the Scene's, logout_disconnect_timeout_ms 500, then tweak(cfg);
    // shape_scene(s), when set, runs last, before the session is built (an EngineConfig change).
    explicit DirectRig(fixpp::session::session_role r,
                       std::function<void(fixpp::session::SessionConfig&)> const& tweak = {},
                       bool with_clock = true,
                       std::function<void(rgs::Scene&)> const& shape_scene = {})
        : role{r}, s{rgs::Options{.role = r, .with_clock = with_clock, .app = app}} {
        bw->record_bytes = true;
        stream->hold_open = true;
        s.cfg.logout_disconnect_timeout_ms = 500;
        // The transports are plaintext, so the profile is too (open()'s factory kind check).
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        s.cfg.security_profile = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (role == fixpp::session::session_role::initiator) {
            s.cfg.transport_factory_override =
                std::make_shared<bwt::BlockingWriteTransportFactory>(stream, bw);
            s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        }
        if (tweak) tweak(s.cfg);
        if (shape_scene) shape_scene(s);
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }

    template <class Awaitable>
    std::optional<fixpp::core::expected_t<void>> run(Awaitable a, const char* site) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  std::move(a), asio::use_future);
        if (!s.pump_until([&] { return rgs::Scene::ready(fut); })) {
            ADD_FAILURE() << site << ": the call did not return";
            return std::nullopt;
        }
        auto r = fut.get();
        s.drain();
        return r;
    }

    // open(), the connection (attached, or made by drive_reconnect()), then the peer's Logon at 1.
    [[nodiscard]] bool to_active() {
        auto const opened = run(s.session->open(), "open");
        if (!opened || !opened->has_value()) {
            ADD_FAILURE() << "DirectRig: open() failed: "
                          << (opened ? static_cast<int>(opened->error()) : -1);
            return false;
        }
        if (role == fixpp::session::session_role::initiator) {
            auto const c = run(s.session->drive_reconnect(), "drive_reconnect");
            if (!c || !c->has_value()) {
                ADD_FAILURE() << "DirectRig: drive_reconnect() failed: "
                              << (c ? static_cast<int>(c->error()) : -1);
                return false;
            }
        } else {
            s.session->attach_accepted_transport(
                std::make_unique<bwt::BlockingWriteTransport>(s.ioc.get_executor(), stream, bw),
                fixpp::transport::handshake_result{});
        }
        (void)s.feed(s.logon(1));
        if (s.state() != fixpp::session::fsm_state::Active) {
            ADD_FAILURE() << "DirectRig: the Logon left the session in state "
                          << static_cast<int>(s.state());
            return false;
        }
        return true;
    }

    // The next write is held; release() lets it complete.
    void hold_next_write() const { bw->arm_block(); }
    void release() const { bw->release(); }
    // The write of the next frame of MsgType type is held: armed at its toAdmin.
    void hold_write_of(std::string type) {
        // The hook holds the state by raw pointer: the application outlives the Scene.
        app->to_admin_hook = [b = bw.get(), t = std::move(type),
                              armed = false](std::string_view m) mutable {
            if (!armed && m == t) {
                armed = true;
                b->arm_block();
            }
        };
    }

    // The transport's close (its ScriptedStream's flag, which every close of a transport the rig
    // made sets).
    [[nodiscard]] bool closed() const { return stream->closed; }
    [[nodiscard]] bool parked() const { return bw->parked; }

    // The completed writes' MsgType(35) values, in write-start order.
    [[nodiscard]] std::size_t completed(std::string_view type) const {
        std::size_t n = 0;
        for (auto const& w : bw->recorded) {
            if (w.completed && fixpp::test_support::extract_tag(w.bytes, 35) == type) ++n;
        }
        return n;
    }
    [[nodiscard]] std::size_t started(std::string_view type) const {
        std::size_t n = 0;
        for (auto const& w : bw->recorded) {
            if (fixpp::test_support::extract_tag(w.bytes, 35) == type) ++n;
        }
        return n;
    }

    // Moves the mock clock and runs every handler that became ready.
    void pass(std::chrono::nanoseconds d) { s.pass(d); }
};

}  // namespace fixpp::test_support::t105
