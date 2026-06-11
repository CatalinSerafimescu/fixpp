// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/test_business_message_interop.cpp — 020 T012/T014/T016
//
// Live business-message interop cells: NOS (35=D) → ExecRpt (35=8), FIX 4.4,
// over TLS, both roles (fixpp-initiator / fixpp-acceptor) vs QuickFIX-J and
// QuickFIX-cpp.
//
// Cells parameterized over (Counterparty ∈ {quickfix_j, quickfix_cpp}) ×
// (Role ∈ {fixpp_initiator, fixpp_acceptor}) — 4 cells total, mirroring
// the 016 US1 hp_fix44_logon_hb_logout_test.cpp template.
//
// T012 [US2]: QuickFIX-J cells (QFj_init, QFj_acc).
// T016 [US3]: QuickFIX-cpp cells (QFcpp_init, QFcpp_acc).
// T014 [US2]: fixpp-acceptor responding Application (RespondingApp below).
//
// All cells SKIP when the counterparty is absent (INTEROP_<TOKEN>_PORT unset
// or unreachable). This is the designed CI behaviour — live runs happen at the
// interop / release tier.
//
// Anchors: tasks.md T012/T014/T016; data-model.md E3; research.md D5/D7/D8;
//          specs/020-g2-business-messages/contracts/business-messages.md;
//          [[feedback_single_threaded_harness_masks_strand_races]] (re-entrant
//          Engine::send from inside fromApp is INV-7 in roundtrip test — this
//          file reuses the same pattern with asio::post+co_spawn off-strand).
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/business_messages.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

// Generated FIX 4.4 read flyweights (codegen 003 — research.md D2/D8).
#include <fixpp/v44/Messages.hpp>

#include "happy/hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::decimal_t;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::session::fsm_state;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a decimal_t from a decimal string literal via parse (same pattern as
// test_business_messages_build.cpp::make_decimal).
static decimal_t make_dec(std::string_view sv, std::pmr::memory_resource* mr) {
    std::vector<std::byte> bytes;
    bytes.reserve(sv.size());
    for (char c : sv) bytes.push_back(static_cast<std::byte>(c));
    auto r = decimal_t::parse(bytes, mr);
    if (!r.has_value()) {
        return decimal_t{};  // callers check via ASSERT
    }
    return *r;
}

// ── T014: fixpp-acceptor responding Application (data-model E3) ──────────────
//
// Unified business Application used by BOTH roles. When fixpp is the acceptor,
// the counterparty-initiator sends a NewOrderSingle; this Application reads it
// via fixpp::v44::NewOrderSingle and replies with a fully-filled ExecutionReport
// via Engine::send called from inside fromApp. When fixpp is the initiator, the
// same fromApp captures the inbound ExecutionReport (35=8) into the er_received
// bundle so the test can assert fidelity — one fixture, one session, no second
// connection (the prior double-fixture design self-collided on duplicate CompIDs).
//
// Re-entrancy contract: Engine::send from inside fromApp is safe because 019's
// any-thread exec-hop + keepalive + RAII send-drain handles it. The call is
// hoisted off the callback strand via asio::post + co_spawn to avoid any
// potential in-strand deadlock (mirrors the INV-7 shape in
// test_business_messages_roundtrip.cpp::SendFromInsideFromApp_NoDeadlockNoUAF).
//
// The Application holds an atomic counter so the test can wait for the reply.
//
// Anchors: data-model E3; tasks.md T014; research.md D5 (fully-filled ExecRpt
// semantics: ExecType='F', OrdStatus='2', LeavesQty=0, CumQty=OrderQty,
// AvgPx=Price); INV-7 (re-entrant Engine::send from fromApp).
class RespondingApp : public Application {
public:
    // Must be set before the session goes Active.
    fixpp::session::Engine* engine = nullptr;
    SessionId acceptor_id;
    asio::any_io_executor exec;

    // Counters for test synchronisation.
    std::atomic<int> nos_received{0};
    std::atomic<int> er_sent{0};
    std::atomic<bool> send_ok{false};

    // ── Initiator-side: capture an inbound ExecutionReport (35=8) ────────────
    std::atomic<bool> er_received{false};
    std::mutex cap_mu;
    char cap_exec_type{'\0'};
    char cap_ord_status{'\0'};
    char cap_side{'\0'};
    std::string cap_symbol;
    std::array<std::byte, 256> cap_arena_buf{};
    std::pmr::monotonic_buffer_resource cap_arena{
        cap_arena_buf.data(), cap_arena_buf.size(), std::pmr::null_memory_resource()};
    decimal_t cap_avg_px{};
    decimal_t cap_cum_qty{};
    decimal_t cap_leaves_qty{};

    // fromApp: respond to NewOrderSingle (acceptor) or capture ExecRpt (initiator).
    fixpp::core::expected_t<void> fromApp(
        const MessageView<access_mode::Index>& msg,
        const SessionId& /*id*/) override
    {
        // Initiator-side: capture an inbound ExecutionReport (35=8).
        if (msg.msg_type() == "8") {
            fixpp::v44::ExecutionReport er{msg};
            std::lock_guard<std::mutex> lk{cap_mu};
            if (auto r = er.exec_type())  cap_exec_type = *r;
            if (auto r = er.ord_status()) cap_ord_status = *r;
            if (auto r = er.symbol())     cap_symbol = std::string{*r};
            if (auto r = er.side())       cap_side = *r;
            if (auto r = er.avg_px(&cap_arena))     cap_avg_px = *r;
            if (auto r = er.cum_qty(&cap_arena))    cap_cum_qty = *r;
            if (auto r = er.leaves_qty(&cap_arena)) cap_leaves_qty = *r;
            er_received.store(true, std::memory_order_release);
            return {};
        }

        // Acceptor-side: only respond to 35=D (NewOrderSingle).
        if (msg.msg_type() != "D") {
            return {};  // ignore other app messages
        }

        ++nos_received;

        // Capture the fields we need to echo. The MessageView lifetime is
        // bounded to the fromApp call, so we must copy strings now.
        fixpp::v44::NewOrderSingle nos{msg};

        // Use a small stack arena for decimal parsing inside fromApp.
        std::array<std::byte, 256> arena_buf{};
        std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                                   std::pmr::null_memory_resource()};

        auto cl_ord_id_r = nos.cl_ord_id();
        auto symbol_r    = nos.symbol();
        auto side_r      = nos.side();
        auto order_qty_r = nos.order_qty(&arena);
        auto price_r     = nos.price(&arena);

        if (!cl_ord_id_r || !symbol_r || !side_r || !order_qty_r || !price_r) {
            // Malformed NOS — reject so the engine can emit BusinessMessageReject.
            return std::unexpected(fixpp::core::error::app_do_not_send);
        }

        // Copy fields for the off-strand closure.
        std::string symbol_str{*symbol_r};
        char side_char = *side_r;
        decimal_t order_qty_val = *order_qty_r;
        decimal_t price_val     = *price_r;

        // Generate fresh IDs for the reply.
        static std::atomic<int> id_counter{0};
        int seq = ++id_counter;
        std::string order_id_str = "ORD" + std::to_string(seq);
        std::string exec_id_str  = "EXC" + std::to_string(seq);

        // Off-strand: build and send the ExecRpt.
        auto* eng = engine;
        auto sid  = acceptor_id;
        auto* er_sent_ptr  = &er_sent;
        auto* send_ok_ptr  = &send_ok;
        auto ex = exec;

        asio::post(exec, [eng, sid, symbol_str = std::move(symbol_str),
                           side_char, order_qty_val, price_val,
                           order_id_str = std::move(order_id_str),
                           exec_id_str  = std::move(exec_id_str),
                           er_sent_ptr, send_ok_ptr, ex]() mutable {
            std::array<std::byte, 512> out_buf{};
            std::span<std::byte> out{out_buf};

            // Fully-filled: ExecType='F', OrdStatus='2', LeavesQty=0,
            // CumQty=OrderQty, AvgPx=Price (research.md D5).
            std::array<std::byte, 64> arena_b{};
            std::pmr::monotonic_buffer_resource ar{arena_b.data(), arena_b.size(),
                                                    std::pmr::null_memory_resource()};
            auto zero = make_dec("0", &ar);

            auto body = fixpp::session::build_execution_report(
                out,
                order_id_str, exec_id_str,
                'F',  // ExecType = Trade (FIX 4.4 fill value)
                '2',  // OrdStatus = Filled
                symbol_str, side_char,
                zero,           // LeavesQty = 0
                order_qty_val,  // CumQty = OrderQty
                price_val       // AvgPx = Price
            );

            if (!body.has_value()) {
                return;
            }

            asio::co_spawn(ex,
                eng->send(sid, *body),
                [er_sent_ptr, send_ok_ptr](std::exception_ptr ep,
                                            fixpp::core::expected_t<void> r) {
                    if (!ep) {
                        if (r.has_value()) {
                            send_ok_ptr->store(true, std::memory_order_release);
                        }
                        er_sent_ptr->fetch_add(1, std::memory_order_release);
                    }
                });
        });

        return {};
    }
};

// ── Test fixture ──────────────────────────────────────────────────────────────

// Value-parameterized over (Counterparty, Role). The same test body covers all
// 4 cells: QFj_init, QFj_acc, QFcpp_init, QFcpp_acc.
class BusinessMessageInterop
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(BusinessMessageInterop, NosExecRptRoundTrip) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── Skip guards (FR-023) ────────────────────────────────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    // ── Responding Application (T014) — used by fixpp-acceptor role ─────────
    // For the initiator role a plain Application suffices (the counterparty is
    // the responder; fixpp only sends the NOS and receives the ExecRpt).
    auto responding_app = std::make_shared<RespondingApp>();

    fixpp::core::EngineConfig ecfg;
    // Inject the responding app for both roles; only fromApp is exercised.
    ecfg.application = responding_app;

    fixpp::interop::InteropEngineFixture fx{std::move(ecfg)};

    auto cfg = hp::make_session_config(role, "FIX.4.4", factory,
                                        fx.ioc().get_executor(), *endpoint);
    const auto id = SessionId::from_config(cfg);

    // Wire up the responding app's back-reference BEFORE registering.
    responding_app->engine      = &fx.engine();
    responding_app->acceptor_id = id;
    responding_app->exec        = fx.ioc().get_executor();

    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Logon: drive to Active ───────────────────────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << "; state=" << static_cast<int>(reached);
    if (reached != fsm_state::Active) {
        hp::expect_graceful_stop(fx);
        return;
    }

    // ── NOS → ExecRpt round-trip (role-dependent) ───────────────────────────
    //
    // fixpp-initiator role (AC1): fixpp sends a NOS, receives an ExecRpt from
    // the counterparty-acceptor which runs its own responding Application.
    // fixpp-acceptor role (AC2): the counterparty-initiator sends a NOS to
    // fixpp; RespondingApp fires fromApp, builds the ExecRpt, sends it back.
    //
    // Both roles assert fixpp's observable: either a received ExecRpt parsed
    // via v44 accessors (initiator), or the RespondingApp's counters (acceptor).

    if (role == Role::fixpp_initiator) {
        // ── fixpp-initiator: send NOS on the live session, await ExecRpt ──────
        //
        // One fixture, one session: send a NOS via Engine::send and wait for the
        // counterparty-acceptor's responding Application to reply with an
        // ExecRpt, which RespondingApp::fromApp captures into er_received.

        // Build the NOS payload.
        std::array<std::byte, 512> nos_buf{};
        std::array<std::byte, 64> dec_arena_buf{};
        std::pmr::monotonic_buffer_resource dec_arena{
            dec_arena_buf.data(), dec_arena_buf.size(), std::pmr::null_memory_resource()};

        auto order_qty = make_dec("100", &dec_arena);
        auto price     = make_dec("190.5", &dec_arena);

        // TransactTime: use a static UTC timestamp (test-only; the counterparty
        // accepts any well-formed UTCTimestamp).
        static constexpr std::string_view kTransactTime = "20240101-00:00:00.000";

        auto nos_body = fixpp::session::build_new_order_single(
            nos_buf, "CLORD001", "AAPL", '1', order_qty, price, kTransactTime);
        ASSERT_TRUE(nos_body.has_value())
            << "build_new_order_single failed; error="
            << static_cast<int>(nos_body.error());

        // Send the NOS via Engine::send (any-thread path).
        {
            auto send_fut = asio::co_spawn(
                fx.ioc().get_executor(),
                fx.engine().send(id, *nos_body),
                asio::use_future);
            fx.run_until(
                [&send_fut]{ return send_fut.wait_for(0ms) == std::future_status::ready; },
                3s);
            ASSERT_TRUE(send_fut.wait_for(0ms) == std::future_status::ready)
                << "Engine::send(NOS) did not complete within 3s";
            auto res = send_fut.get();
            EXPECT_TRUE(res.has_value())
                << "Engine::send(NOS) failed; error=" << static_cast<int>(res.error());
        }

        // Wait for the ExecRpt to arrive in fromApp.
        fx.run_until(
            [&responding_app]{
                return responding_app->er_received.load(std::memory_order_acquire);
            },
            5s);

        EXPECT_TRUE(responding_app->er_received.load(std::memory_order_acquire))
            << "No ExecutionReport received from counterparty within 5s";

        if (responding_app->er_received.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk{responding_app->cap_mu};
            // Fidelity assertions (FR-010/013; data-model D5; INV-3).
            EXPECT_EQ(responding_app->cap_exec_type, 'F')
                << "ExecType must be 'F' (Trade / fully-filled)";
            EXPECT_EQ(responding_app->cap_ord_status, '2')
                << "OrdStatus must be '2' (Filled)";
            EXPECT_EQ(responding_app->cap_symbol, "AAPL")
                << "Symbol must echo the order's Symbol";
            EXPECT_EQ(responding_app->cap_side, '1')
                << "Side must echo the order's Side";

            // Decimal value-equality (INV-3): 190.5 == 190.50, etc.
            std::array<std::byte, 64> cmp_arena_buf{};
            std::pmr::monotonic_buffer_resource cmp_arena{
                cmp_arena_buf.data(), cmp_arena_buf.size(),
                std::pmr::null_memory_resource()};
            auto expected_qty  = make_dec("100",   &cmp_arena);
            auto expected_px   = make_dec("190.5", &cmp_arena);
            auto expected_zero = make_dec("0",     &cmp_arena);
            // CumQty == OrderQty (fully-filled).
            EXPECT_EQ(responding_app->cap_cum_qty, expected_qty)
                << "CumQty must equal OrderQty (100)";
            // AvgPx == Price.
            EXPECT_EQ(responding_app->cap_avg_px, expected_px)
                << "AvgPx must equal Price (190.5)";
            // LeavesQty == 0.
            EXPECT_EQ(responding_app->cap_leaves_qty, expected_zero)
                << "LeavesQty must be 0 (fully filled)";
        }

        // Settle: let the counterparty flush its buffered transcript tail (the
        // inbound ExecRpt + Logout) before teardown. fixpp tears down the instant
        // er_received flips, which can race a buffered QuickFIX-J transcript write
        // and capture a short golden; a brief wall-clock settle makes it stable.
        fx.run_until([]{ return false; }, 500ms);
        hp::expect_graceful_stop(fx);

    } else {
        // ── fixpp-acceptor: counterparty sends NOS, we reply ExecRpt ─────────
        //
        // The counterparty-initiator (QFJ or QFcpp) sends a NOS to fixpp.
        // RespondingApp::fromApp fires, builds the ExecRpt, sends it.
        // We assert fixpp's observable: NOS received + ExecRpt originated.

        // Wait up to 10 s for the counterparty to send a NOS and for us to
        // reply with an ExecRpt.
        fx.run_until(
            [&responding_app]{
                return responding_app->er_sent.load(std::memory_order_acquire) >= 1;
            },
            10s);

        EXPECT_GE(responding_app->nos_received.load(std::memory_order_acquire), 1)
            << "fixpp acceptor: no NewOrderSingle received from counterparty within 10s";
        EXPECT_GE(responding_app->er_sent.load(std::memory_order_acquire), 1)
            << "fixpp acceptor: ExecutionReport was not sent after receiving NOS";
        EXPECT_TRUE(responding_app->send_ok.load(std::memory_order_acquire))
            << "fixpp acceptor: Engine::send(ExecRpt) failed inside fromApp";

        // Session must still be Active after the app exchange.
        {
            auto s = fx.engine().lookup(id);
            EXPECT_NE(s, nullptr) << "session must still exist after exchange";
            if (s != nullptr) {
                EXPECT_EQ(s->state(), fsm_state::Active)
                    << "session must stay Active after NOS/ExecRpt exchange";
            }
        }

        // Settle: let the counterparty flush its buffered transcript tail (the
        // inbound ExecRpt + Logout) before teardown — see the initiator branch.
        fx.run_until([]{ return false; }, 500ms);
        hp::expect_graceful_stop(fx);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, BusinessMessageInterop,
    ::testing::Combine(
        ::testing::Values(Counterparty::quickfix_j, Counterparty::quickfix_cpp),
        ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
