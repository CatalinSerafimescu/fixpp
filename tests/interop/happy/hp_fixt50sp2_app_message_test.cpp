// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fixt50sp2_app_message_test.cpp — 095-tail-evidence-pass US3 (live).
//
// One typed-built application message over FIXT.1.1 / FIX.5.0SP2: fixpp (initiator) logs on with
// DefaultApplVerID(1137)=9, sends one 35=BN built by fixpp::v50sp2::build_ExecutionAck from step
// F-01 of tests/interop/conversation/fixt_app_script.yaml, and logs out. The QuickFIX peer
// (QuickFIX-cpp or QuickFIX-J, UseDataDictionary=Y over FIXT11.xml + FIX50SP2.xml) writes its
// generic per-field readback of the BN it received; compare_streams compares it with fixpp's
// `sent` record (the live-cell contract's clauses L1 to L4).
//
// The cell's record (FR-013): fixpp's inbound validation is OFF on this cell
// (SessionConfig::validate_inbound_messages = false; the hello carries arm "validation-off").
// The reason is L-041-2: the session validator takes one dictionary, and a FIXT session needs
// two (FIXT11.xml for the session layer, FIX50SP2.xml for the application layer). The cell's
// evidence is the PEER's dictionary-backed parse of fixpp's message, which does not depend on
// fixpp's inbound validation.
//
// Second source of the 033 FIXT target interop_hp_fixt50sp2_logon_hb_logout_test; 033's
// HappyFixtLogonHbLogout cells live in their own file. Gtest ids (contracts L0):
// Fixt/HappyFixtAppMessage.TypedExecutionAck/{QFcpp,QFj}_init_fix50sp2; the parent harness's
// gtest_filter and tests/interop/expected-skips-without-counterparty.txt must name them verbatim.
//
// Pass conditions, all evaluated whatever the others return: exactly one comparison row and it
// passes; no Reject(35=3) from the peer; Engine::stop() completes (hp::expect_graceful_stop). The
// harness adds the peer's trailing LOGOUT transcript line. After Engine start every check is
// non-fatal until the engine has stopped, so a failure reports instead of aborting in ~Engine.
// Peer Rejects are read from the peer's own transcript (`OUT 3` / `OUT j` lines), because fixpp
// does not dispatch an inbound Reject to fromAdmin; tracked as fixpp#612.
//
// Counterparty-required (FR-023): absent ⇒ GTEST_SKIP through INTEROP_REQUIRE_COUNTERPARTY, the
// first statement that can skip or return.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/v50sp2/messages/ExecutionAck.hpp>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hp_support.hpp"
#include "support/counterparty_probe.hpp"
#include "support/intent_file.hpp"
#include "support/readback_jsonl.hpp"
#include "support/session_test_access.hpp"
#include "support/witness_comparator.hpp"

using namespace std::chrono_literals;
namespace hp = fixpp::interop::hp;
namespace rb = fixpp::interop::readback;
namespace intent = fixpp::interop::intent;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::Application;
using fixpp::session::fsm_state;
using fixpp::session::SessionId;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace {

std::string env_or_empty(char const* key) {
    char const* v =
        std::getenv(key);  // NOLINT(concurrency-mt-unsafe) -- single-threaded test setup
    return v == nullptr ? std::string() : std::string(v);
}

std::string sha256_hex_file(std::string const& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream oss;
    oss << f.rdbuf();
    std::string const data = oss.str();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<unsigned char const*>(data.data()), data.size(), digest);
    static char const* kHex = "0123456789abcdef";
    std::string out(SHA256_DIGEST_LENGTH * 2, '\0');
    for (std::size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out[2 * i] = kHex[(digest[i] >> 4) & 0x0F];
        out[(2 * i) + 1] = kHex[digest[i] & 0x0F];
    }
    return out;
}

std::string run_dir_of(std::string const& readback_path) {
    std::size_t const slash = readback_path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : readback_path.substr(0, slash);
}

fixpp::decimal_t decimal_from(std::string const& v, std::pmr::memory_resource* mr) {
    auto r = fixpp::decimal_t::parse(
        std::span<std::byte const>{reinterpret_cast<std::byte const*>(v.data()), v.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "F-01 value '" << v << "' is not a decimal";
    return r.value_or(fixpp::decimal_t{});
}

class AppMessageApp : public Application {
public:
    rb::Stream* stream = nullptr;

    // Every stream write before stream.hello() is deferred until it is written, so the hello
    // stays the stream's first line (the 089 drivers' rule).
    std::mutex hello_mu;
    bool hello_written = false;
    std::vector<std::function<void()>> pending_before_hello;

    template <typename F>
    void write_or_defer(F&& write_call) {
        std::scoped_lock lk(hello_mu);
        if (hello_written) {
            write_call();
        } else {
            pending_before_hello.emplace_back(std::forward<F>(write_call));
        }
    }

    void mark_hello_written() {
        std::scoped_lock lk(hello_mu);
        for (auto& fn : pending_before_hello) fn();
        pending_before_hello.clear();
        hello_written = true;
    }

    // Armed right before Engine::send(); consumed by toApp, which adds the one captured value,
    // MsgSeqNum(34), the pairing key.
    struct PendingSent {
        std::string script_step_id;
        std::vector<rb::FieldEntry> fields;
    };
    std::optional<PendingSent> pending_sent;

    void arm_pending_sent(std::string step_id, std::vector<rb::FieldEntry> fields) {
        pending_sent =
            PendingSent{.script_step_id = std::move(step_id), .fields = std::move(fields)};
    }

    fixpp::core::expected_t<void> toApp(MessageView<access_mode::Index> const& msg,
                                        SessionId const& /*id*/) override {
        if (stream != nullptr && pending_sent.has_value()) {
            PendingSent p = std::move(*pending_sent);
            pending_sent.reset();
            long long const seq = msg.msg_seq_num();
            rb::Stream* s = stream;
            write_or_defer([s, mt = std::string(msg.msg_type()), seq, step_id = p.script_step_id,
                            fields = std::move(p.fields)]() mutable {
                s->sent(mt, seq, rb::kDirectionFixppToPeer, 0, step_id, std::move(fields));
            });
        }
        return {};
    }
};

// The peer's readback of a fixpp-to-peer `msg_type` has landed in its stream. A record that
// parse_stream refuses reads as "not yet": the comparison after the stop reads the same file and
// reports it.
bool peer_readback_landed(std::string const& cp_path, std::string const& msg_type) noexcept {
    try {
        for (auto const& r : rb::parse_stream(cp_path)) {
            if (r.kind == rb::ParsedRecord::Kind::Readback && r.msg_type == msg_type &&
                r.direction == std::string(rb::kDirectionFixppToPeer)) {
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

// Every Reject(35=3) or BusinessMessageReject(35=j) the peer sent, read from its transcript: both
// counterparties write each outbound admin and application frame there as `OUT <MsgType> <frame>`
// before sending it. Read there, not in fromAdmin: fixpp's session accepts an inbound Reject
// without dispatching it to fromAdmin (Session's I-5 rule, src/session/session.cpp).
std::vector<std::string> peer_rejects_in(std::string const& transcript_path) {
    std::vector<std::string> out;
    std::ifstream f(transcript_path, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (line.starts_with("OUT 3 ") || line.starts_with("OUT j ")) out.push_back(line);
    }
    return out;
}

std::string join_lines(std::vector<std::string> const& lines) {
    std::string out;
    for (auto const& l : lines) out += (out.empty() ? "" : "; ") + l;
    return out;
}

class HappyFixtAppMessage : public ::testing::TestWithParam<Counterparty> {};

// "QFcpp_init_fix50sp2" / "QFj_init_fix50sp2" (033's fixt_cell_name shape, contracts L0).
std::string app_cell_name(::testing::TestParamInfo<Counterparty> const& info) {
    return std::string(info.param == Counterparty::quickfix_cpp ? "QFcpp" : "QFj") +
           "_init_fix50sp2";
}

TEST_P(HappyFixtAppMessage, TypedExecutionAck) {
    Counterparty const counterparty = GetParam();
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // ── Run identity (data-model §1/§9): set by the parent harness for a scripted cell. ──
    std::string const run_id = env_or_empty("INTEROP_FIXPP_RUN_ID");
    std::string const cell_id = env_or_empty("INTEROP_FIXPP_CELL_ID");
    std::string const config = env_or_empty("INTEROP_FIXPP_CONFIG");
    std::string const arm = env_or_empty("INTEROP_FIXPP_ARM");
    std::string const combo = env_or_empty("INTEROP_FIXPP_COMBO_ID");
    std::string const script_path = env_or_empty("INTEROP_FIXPP_SCRIPT_PATH");
    std::string const script_digest_expected = env_or_empty("INTEROP_FIXPP_SCRIPT_DIGEST");
    std::string const readback_path = env_or_empty("INTEROP_FIXPP_READBACK_PATH");
    std::string const intent_path = env_or_empty("INTEROP_FIXPP_INTENT_PATH");
    ASSERT_FALSE(run_id.empty()) << "INTEROP_FIXPP_RUN_ID absent";
    ASSERT_FALSE(cell_id.empty()) << "INTEROP_FIXPP_CELL_ID absent";
    ASSERT_FALSE(config.empty()) << "INTEROP_FIXPP_CONFIG absent";
    ASSERT_EQ(arm, "validation-off") << "this cell runs with fixpp's inbound validation off";
    ASSERT_EQ(combo, counterparty == Counterparty::quickfix_cpp ? "C1" : "C3")
        << "the registration's combo for this counterparty";
    ASSERT_FALSE(script_path.empty()) << "INTEROP_FIXPP_SCRIPT_PATH absent";
    ASSERT_FALSE(script_digest_expected.empty()) << "INTEROP_FIXPP_SCRIPT_DIGEST absent";
    ASSERT_FALSE(readback_path.empty()) << "INTEROP_FIXPP_READBACK_PATH absent";
    ASSERT_FALSE(intent_path.empty()) << "INTEROP_FIXPP_INTENT_PATH absent";

    std::string const actual_digest = sha256_hex_file(script_path);
    ASSERT_EQ(actual_digest, script_digest_expected) << "script_digest mismatch";

    // ── Step F-01's values, from the rendered intent file. ──
    std::vector<intent::Message> const messages = intent::parse_intent_file(intent_path);
    intent::Message const* f01 = nullptr;
    for (auto const& m : messages) {
        if (m.step_id == "F-01" && m.originator == "fixpp") f01 = &m;
    }
    ASSERT_NE(f01, nullptr) << "no fixpp intent entry for step F-01 in " << intent_path;
    ASSERT_EQ(f01->msg_type, "BN") << "step F-01 is not a 35=BN";

    // The `sent` record: the F-01 literals, fixed before the build (data-model §D).
    std::vector<rb::FieldEntry> sent_fields;
    for (auto const& f : f01->fields) sent_fields.push_back({.path = f.path, .value = f.value});

    // The builder's input: the same F-01 literals, set member by member.
    std::pmr::monotonic_buffer_resource arena{1024};
    fixpp::v50sp2::ExecutionAckArgs args{};
    for (auto const& f : f01->fields) {
        if (f.path == "37") {
            args.order_id = f.value;
        } else if (f.path == "1036") {
            ASSERT_EQ(f.value.size(), 1U) << "ExecAckStatus(1036) is a char";
            args.exec_ack_status = f.value[0];
        } else if (f.path == "17") {
            args.exec_id = f.value;
        } else if (f.path == "54") {
            ASSERT_EQ(f.value.size(), 1U) << "Side(54) is a char";
            args.side = f.value[0];
        } else if (f.path == "55") {
            args.symbol = f.value;
        } else if (f.path == "38") {
            args.order_qty = decimal_from(f.value, &arena);
        } else if (f.path == "11") {
            args.cl_ord_id = f.value;
        } else if (f.path == "14") {
            args.cum_qty = decimal_from(f.value, &arena);
        } else if (f.path == "6") {
            args.avg_px = decimal_from(f.value, &arena);
        } else {
            FAIL() << "step F-01 path " << f.path << " has no ExecutionAckArgs member here";
        }
    }
    std::array<std::byte, 4096> out{};
    auto const built = fixpp::v50sp2::build_ExecutionAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionAck refused the F-01 build";

    // ── Session: FIXT.1.1, DefaultApplVerID 9, production dictionaries (contracts L1). ──
    auto prod = hp::production_fixt50sp2_engine_config();
    char const* dir = hp::tls_fixture_dir();
    ASSERT_TRUE(dir != nullptr && dir[0] != '\0') << "FIXPP_TLS_FIXTURE_DIR not set";
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";
    auto const endpoint = hp::cell_endpoint(counterparty, Role::fixpp_initiator);
    ASSERT_TRUE(endpoint.has_value()) << "cell endpoint unresolved";

    auto app = std::make_shared<AppMessageApp>();
    prod.engine_config.application = app;
    fixpp::interop::InteropEngineFixture fx{std::move(prod.engine_config)};

    auto cfg = hp::make_session_config(Role::fixpp_initiator, "FIXT.1.1", factory,
                                       fx.ioc().get_executor(), *endpoint);
    cfg.default_appl_ver_id = fixpp::dict::application_version::v50sp2;
    cfg.dictionary = prod.session_dictionary;
    cfg.validate_inbound_messages = false;
    auto const id = SessionId::from_config(cfg);

    rb::Stream stream(readback_path);
    ASSERT_TRUE(stream.ok()) << "cannot open fixpp readback stream: " << readback_path;
    app->stream = &stream;

    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";
    fx.start();

    // ── From here every check is non-fatal until the engine has stopped. ──
    auto const reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "FIXT session did not reach Active against " << hp::counterparty_token(counterparty);

    auto sess = fx.engine().lookup(id);
    bool const has_validator =
        sess != nullptr && fixpp::session::session_test_access::has_validator(*sess);
    EXPECT_FALSE(has_validator) << "inbound validation must be off on this cell (FR-013)";
    stream.hello(run_id, cell_id, config, actual_digest, arm, has_validator,
                 prod.app_dictionary_digest);
    app->mark_hello_written();

    std::string const cp_path = run_dir_of(readback_path) + "/counterparty-readback.jsonl";
    std::string const cp_transcript = run_dir_of(readback_path) + "/counterparty-transcript.txt";
    if (reached == fsm_state::Active) {
        app->arm_pending_sent("F-01", sent_fields);
        auto fut =
            asio::co_spawn(fx.ioc().get_executor(), fx.engine().send(id, *built), asio::use_future);
        fx.run_until([&] { return fut.wait_for(0ms) == std::future_status::ready; }, 3s);
        if (fut.wait_for(0ms) != std::future_status::ready) {
            ADD_FAILURE() << "Engine::send of the 35=BN did not complete within 3s";
        } else {
            auto const r = fut.get();
            EXPECT_TRUE(r.has_value())
                << "Engine::send of the 35=BN failed; error=" << static_cast<int>(r.error());
        }
        // The peer's verdict on the BN: its readback, or a Reject.
        bool const settled = fx.run_until(
            [&] {
                return !peer_rejects_in(cp_transcript).empty() ||
                       peer_readback_landed(cp_path, "BN");
            },
            5s);
        EXPECT_TRUE(settled) << "within 5s the peer neither read back nor rejected the 35=BN";
    }

    hp::expect_graceful_stop(fx);
    stream.terminal("completed", run_id, cell_id, config, actual_digest);

    // ── The peer-Reject check: evaluated whatever the comparison below returns. ──
    std::vector<std::string> const peer_rejects = peer_rejects_in(cp_transcript);
    EXPECT_TRUE(peer_rejects.empty())
        << "the peer rejected fixpp's message: " << join_lines(peer_rejects);

    // ── The comparison (contracts L4). ──
    rb::WitnessIdentity wid;
    wid.run_id = run_id;
    wid.combo_id = combo;
    wid.cell_id = cell_id;
    wid.config = config;
    wid.arm = arm;
    wid.kind = "app-message-evidence";
    wid.authoritative = true;
    auto is_decimal = rb::make_fix44_decimal_resolver(prod.app_dictionary_path);
    auto const rows = rb::compare_streams(rb::parse_stream(readback_path),
                                          rb::parse_stream(cp_path), wid, is_decimal);
    EXPECT_TRUE(rb::write_witness_rows(run_dir_of(readback_path) + "/witnesses.jsonl", rows))
        << "failed to write witnesses.jsonl";
    EXPECT_EQ(rows.size(), 1U) << "exactly one comparison row is required (the F-01 35=BN); got "
                               << rows.size();
    for (auto const& row : rows) {
        EXPECT_EQ(row.verdict, "pass")
            << "comparison row " << row.witness_id << " (" << row.msg_type << ") failed";
        for (auto const& m : row.mismatch) {
            ADD_FAILURE() << "  " << row.witness_id << " " << m.cls << " path=" << m.path
                          << " sent=" << m.sent_value << " readback=" << m.readback_value;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Fixt, HappyFixtAppMessage,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         app_cell_name);

}  // namespace
