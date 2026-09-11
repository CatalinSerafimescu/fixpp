// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/conversation/probe_cell_test.cpp — 089 T062a/T062b: the
// FR-010a divergence probe driver. A SEPARATE, minimal gtest binary from
// conv_cell_test.cpp (Conversation.Cell) -- deliberately NOT a branch
// inside that file's hardcoded 12-step B-01..B-12 control flow, which
// would have been exactly the "second hard-wired path" 089's own review
// discipline warns against. This driver's control flow is instead the
// smallest thing the probe needs: A-LOGON -> wait for ONE inbound
// disposition (accepted via fromApp, or rejected via fixpp's own outbound
// Reject observed in toAdmin) -> fixpp initiates Logout
// (hp::expect_graceful_stop). No B-0N sends, no A-TESTREQ/A-GAPFILL/
// A-REJECT admin repertoire -- probe_script.yaml declares none of it.
//
// [const §XV.9]: tests/-only.
#include <gtest/gtest.h>

#include <openssl/sha.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "happy/hp_support.hpp"
#include "support/counterparty_probe.hpp"
#include "support/readback_jsonl.hpp"

using namespace std::chrono_literals;
namespace hp = fixpp::interop::hp;
namespace rb = fixpp::interop::readback;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::session::fsm_state;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

std::string env_or_empty(char const* key)
{
    char const* v = std::getenv(key);  // NOLINT(concurrency-mt-unsafe) -- single-threaded test setup
    return v == nullptr ? std::string() : std::string(v);
}

std::string sha256_hex_file(std::string const& path)
{
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

// The Application driving the probe. Generic over BOTH arms: on the off arm
// fixpp's validator is disabled so the probe's NewOrderSingle (missing
// TransactTime(60)) is delivered to fromApp and accepted; on the on arm the
// validator gate rejects it before FSM delivery, and fixpp's OWN outbound
// Reject(35=3) is observed via toAdmin. Exactly one of the two fires per
// run -- both set `settled` so the TEST body's single wait covers both.
class ProbeApp : public Application {
public:
    rb::Stream* stream = nullptr;
    std::atomic<bool> settled{false};

    std::mutex hello_mu;
    bool hello_written = false;
    std::vector<std::function<void()>> pending_before_hello;

    template <typename F>
    void write_or_defer(F&& write_call)
    {
        std::lock_guard<std::mutex> lk(hello_mu);
        if (hello_written) {
            write_call();
        } else {
            pending_before_hello.emplace_back(std::forward<F>(write_call));
        }
    }

    void mark_hello_written()
    {
        std::lock_guard<std::mutex> lk(hello_mu);
        for (auto& fn : pending_before_hello) fn();
        pending_before_hello.clear();
        hello_written = true;
    }

    // data-model §13/T061a's same shared-counter rule: seq_num+direction
    // alone is a sufficient occurrence key -- the probe sends exactly one
    // peer-originated message, so occurrence is always 0.
    fixpp::core::expected_t<void> fromApp(MessageView<access_mode::Index> const& msg,
                                          SessionId const& /*id*/) override
    {
        if (stream != nullptr) {
            long long const seq = msg.msg_seq_num();
            rb::Stream* s = stream;
            std::string const mt(msg.msg_type());
            write_or_defer([s, mt, seq]() {
                s->disposition(mt, seq, rb::kDirectionPeerToFixpp, 0, "accepted");
            });
        }
        settled.store(true);
        return {};
    }

    void toAdmin(MessageView<access_mode::Index> const& msg, SessionId const& /*id*/) override
    {
        if (stream == nullptr || msg.msg_type() != "3") {
            return;
        }
        auto ref_seq_fv = msg.get(45);
        if (!ref_seq_fv.has_value()) {
            return;  // malformed Reject -- nothing to join a disposition to
        }
        long long const ref_seq = std::stoll(std::string(ref_seq_fv->as_string()));
        std::string ref_msg_type;
        if (auto fv = msg.get(372); fv.has_value()) {
            ref_msg_type = std::string(fv->as_string());
        }
        rb::RejectInfo reject;
        reject.ref_seq_num = ref_seq;
        if (auto fv = msg.get(373); fv.has_value()) {
            reject.reason = std::stoi(std::string(fv->as_string()));
        }
        if (auto fv = msg.get(371); fv.has_value()) {
            reject.ref_tag = std::stoi(std::string(fv->as_string()));
        }
        if (auto fv = msg.get(58); fv.has_value()) {
            reject.text = std::string(fv->as_string());
        }
        rb::Stream* s = stream;
        write_or_defer([s, ref_msg_type, ref_seq, reject]() {
            s->disposition(ref_msg_type, ref_seq, rb::kDirectionPeerToFixpp, 0, "rejected", reject);
        });
        settled.store(true);
    }
};

}  // namespace

TEST(Probe, Cell)
{
    // FR-023: standard skip-with-reason convention -- see conv_cell_test.cpp's
    // TEST(Conversation, Cell) header for the same discipline.
    INTEROP_REQUIRE_COUNTERPARTY("quickfix-cpp");

    std::string const run_id = env_or_empty("INTEROP_FIXPP_RUN_ID");
    std::string const cell_id = env_or_empty("INTEROP_FIXPP_CELL_ID");
    std::string const config = env_or_empty("INTEROP_FIXPP_CONFIG");
    std::string const arm = env_or_empty("INTEROP_FIXPP_ARM");
    std::string const combo = env_or_empty("INTEROP_FIXPP_COMBO_ID");
    std::string const script_path = env_or_empty("INTEROP_FIXPP_SCRIPT_PATH");
    std::string const script_digest_expected = env_or_empty("INTEROP_FIXPP_SCRIPT_DIGEST");
    std::string const readback_path = env_or_empty("INTEROP_FIXPP_READBACK_PATH");
    ASSERT_FALSE(run_id.empty()) << "INTEROP_FIXPP_RUN_ID absent";
    ASSERT_FALSE(cell_id.empty()) << "INTEROP_FIXPP_CELL_ID absent";
    ASSERT_FALSE(config.empty()) << "INTEROP_FIXPP_CONFIG absent";
    ASSERT_FALSE(arm.empty()) << "INTEROP_FIXPP_ARM absent";
    ASSERT_FALSE(script_path.empty()) << "INTEROP_FIXPP_SCRIPT_PATH absent";
    ASSERT_FALSE(script_digest_expected.empty()) << "INTEROP_FIXPP_SCRIPT_DIGEST absent";
    ASSERT_FALSE(readback_path.empty()) << "INTEROP_FIXPP_READBACK_PATH absent";
    // T062a: combo C1 only, matches this cell's registration.
    ASSERT_EQ(combo, "C1") << "probe driver only implements combo C1";

    std::string const actual_digest = sha256_hex_file(script_path);
    ASSERT_EQ(actual_digest, script_digest_expected) << "script_digest mismatch";

    auto prod = hp::production_dictionary_and_digest();

    char const* dir = hp::tls_fixture_dir();
    ASSERT_NE(dir, nullptr) << "FIXPP_TLS_FIXTURE_DIR unset";
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";
    auto const endpoint = hp::cell_endpoint(Counterparty::quickfix_cpp, Role::fixpp_initiator);
    ASSERT_TRUE(endpoint.has_value()) << "cell endpoint unresolved";

    auto app = std::make_shared<ProbeApp>();
    fixpp::core::EngineConfig ecfg;
    ecfg.application = app;
    fixpp::interop::InteropEngineFixture fx{std::move(ecfg)};

    auto cfg = hp::make_session_config(Role::fixpp_initiator, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    cfg.dictionary = prod.dictionary;
    cfg.validate_inbound_messages = (arm == "validation-on");
    cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
    auto const id = SessionId::from_config(cfg);

    rb::Stream stream(readback_path);
    ASSERT_TRUE(stream.ok()) << "cannot open fixpp readback stream: " << readback_path;
    app->stream = &stream;

    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value()) << "register_session failed";
    fx.start();

    auto const reached = hp::drive_to_active(fx, id, 5s);
    ASSERT_EQ(reached, fsm_state::Active) << "session did not reach Active";

    auto sess = fx.engine().lookup(id);
    ASSERT_NE(sess, nullptr);

    bool const has_validator = sess->has_validator_for_test();
    stream.hello(run_id, cell_id, config, actual_digest, arm, has_validator, prod.dictionary_digest);
    app->mark_hello_written();
    // FR-011a / E-6: a validation-on arm MUST attest a live validator.
    EXPECT_EQ(has_validator, arm == "validation-on")
        << "has_validator (" << has_validator << ") disagrees with arm " << arm;

    // Wait for the probe's single inbound arrival to settle (either
    // fromApp's "accepted" on the off arm, or toAdmin's Reject-driven
    // "rejected" on the on arm).
    bool const observed = fx.run_until([&] { return app->settled.load(); }, 5s);
    EXPECT_TRUE(observed) << "T062: probe message P-01 never settled (neither accepted nor "
                             "rejected) within 5s";

    // fixpp initiates Logout.
    hp::expect_graceful_stop(fx);

    stream.terminal("completed", run_id, cell_id, config, actual_digest);
}
