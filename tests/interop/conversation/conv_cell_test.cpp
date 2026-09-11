// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/conversation/conv_cell_test.cpp — 089 Phase 5 (US3): the
// combo-neutral conversation driver. ONE gtest binary/TEST body for every
// combo C1-C4 (C1 -- fixpp INITIATOR vs QuickFIX-cpp -- and C2 -- fixpp
// ACCEPTOR vs QuickFIX-cpp -- are implemented so far; C3-C4 join in later
// rounds), selected/configured entirely by the shim's environment
// (INTEROP_FIXPP_*): `combo` (INTEROP_FIXPP_COMBO_ID) picks the combo (and,
// via `role` below, the transport role), `arm` decides
// `validate_inbound_messages`, everything else is run-identity metadata
// (data-model.md §1/§9). An unrecognised or not-yet-implemented combo value
// fails closed (the ASSERT_TRUE set-membership check below) rather than
// silently running the wrong script under the wrong identity. The control
// FLOW below (which step follows which) is hardcoded per FR-008d(a) — no
// side may carry a YAML parser — but every FIELD VALUE fixpp sends is read
// at run time from the shim-rendered intent file (FR-008d), never a
// literal.
//
// Admin repertoire (T056): A-LOGON is drive_to_active(); A-TESTREQ (peer
// TestRequest -> fixpp Heartbeat) and A-GAPFILL (fixpp ResendRequest -> peer
// SequenceReset-GapFill) are pure session-layer / engine-automatic behaviour
// on BOTH sides -- no application code sends either message; see the
// counterparty-side induction in interop_counterparty_main.cpp
// (setNextSenderMsgSeqNum + a throwaway stimulus Heartbeat after replying to
// A-TESTREQ). A-REJECT is the one admin exchange fixpp must actively
// originate: a TestRequest carrying an out-of-context Symbol(55), sent via
// the sanctioned FIXPP_TEST_HOOKS seam (Session::seqnum_mgr_test_access() +
// store_then_emit_test_access()) because Engine::send() is scoped to
// APPLICATION messages (it runs toApp + the durable outbound-store path) and
// fixpp exposes no public "send an arbitrary/malformed admin message" API --
// see the implementation report for why this seam, not a production
// addition, was chosen.
//
// Business steps (T052/T053a/T054): B-01/B-03/B-05 sent from the intent
// file's fixpp-originated fields via the generic, group-aware builder in
// conversation/support/conv_wire.hpp (C-8: never re-read from the frame).
// B-02/B-04/B-06 are the peer's replies, captured via fromApp's generic body
// walk into a `readback` record (data-model.md §2). B-07/B-09/B-11 are
// peer-originated and trigger fixpp's OWN typed-read-tier-backed reply
// (B-08/B-10/B-12, T054) reactively from inside fromApp, off-strand
// (asio::post + co_spawn, the INV-7 pattern test_business_message_interop.cpp
// already established for re-entrant Engine::send from a callback).
//
// [const §XV.9]: tests/-only.
#include <gtest/gtest.h>

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include <fixpp/v44/Messages.hpp>

#include "conversation/support/conv_wire.hpp"
#include "happy/hp_support.hpp"
#include "support/counterparty_probe.hpp"
#include "support/intent_file.hpp"
#include "support/readback_jsonl.hpp"
#include "support/witness_comparator.hpp"

using namespace std::chrono_literals;
namespace hp = fixpp::interop::hp;
namespace intent = fixpp::interop::intent;
namespace conv = fixpp::interop::conversation;
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

std::string now_utc_ms()
{
    auto const now = std::chrono::system_clock::now();
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t const t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03d", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                 static_cast<int>(ms.count()));
    return buf;
}

std::string dec_to_str(fixpp::decimal_t const& d)
{
    std::array<std::byte, 64> buf{};
    auto r = d.format(buf);
    if (!r.has_value()) return {};
    return std::string(reinterpret_cast<char const*>(buf.data()), *r);
}

std::string typed_val(int tag, std::string raw)
{
    return rb::canonical_typed_value(conv::fix_type_for_tag(tag), raw);
}

// T054: typed reads for the six peer->fixpp business steps, via fixpp's OWN
// generated typed accessors (fixpp::v44::<Message>) -- not the generic body
// walk `fields` already carries. Best-effort by msg_type; a field the
// message declares but this cell's script does not exercise is simply
// omitted (typed_reads is descriptive only -- witness_comparator.hpp's
// ParsedRecord does not read it; FR-006 ranges over `fields`).
std::vector<rb::TypedEntry> typed_reads_for(std::string const& mt,
                                            MessageView<access_mode::Index> const& msg)
{
    std::vector<rb::TypedEntry> out;
    std::array<std::byte, 256> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    auto push_sv = [&](int tag, fixpp::core::expected_t<std::string_view> r) {
        if (r.has_value()) out.push_back({std::to_string(tag), conv::fix_type_for_tag(tag),
                                          typed_val(tag, std::string(*r))});
    };
    auto push_char = [&](int tag, fixpp::core::expected_t<char> r) {
        if (r.has_value()) out.push_back({std::to_string(tag), conv::fix_type_for_tag(tag),
                                          typed_val(tag, std::string(1, *r))});
    };
    auto push_int = [&](int tag, fixpp::core::expected_t<std::int32_t> r) {
        if (r.has_value()) out.push_back({std::to_string(tag), conv::fix_type_for_tag(tag),
                                          typed_val(tag, std::to_string(*r))});
    };
    auto push_dec = [&](int tag, fixpp::core::expected_t<fixpp::decimal_t> r) {
        if (r.has_value()) out.push_back({std::to_string(tag), conv::fix_type_for_tag(tag),
                                          typed_val(tag, dec_to_str(*r))});
    };

    if (mt == "8") {  // ExecutionReport: B-02, B-06
        fixpp::v44::ExecutionReport er{msg};
        push_char(150, er.exec_type());
        push_char(39, er.ord_status());
        push_char(54, er.side());
        push_dec(151, er.leaves_qty(&arena));
        push_dec(14, er.cum_qty(&arena));
        push_dec(6, er.avg_px(&arena));
        push_sv(55, er.symbol());
        push_sv(37, er.order_id());
        push_sv(17, er.exec_id());
    } else if (mt == "9") {  // OrderCancelReject: B-04
        fixpp::v44::OrderCancelReject ocr{msg};
        push_sv(11, ocr.cl_ord_id());
        push_sv(41, ocr.orig_cl_ord_id());
        push_char(39, ocr.ord_status());
        push_char(434, ocr.cxl_rej_response_to());
        push_int(102, ocr.cxl_rej_reason());
        push_sv(37, ocr.order_id());
    } else if (mt == "D") {  // NewOrderSingle: B-07
        fixpp::v44::NewOrderSingle nos{msg};
        push_sv(11, nos.cl_ord_id());
        push_char(54, nos.side());
        push_char(40, nos.ord_type());
        push_sv(60, nos.transact_time());
        push_sv(55, nos.symbol());
        push_dec(38, nos.order_qty(&arena));
        push_dec(44, nos.price(&arena));
    } else if (mt == "F") {  // OrderCancelRequest: B-09
        fixpp::v44::OrderCancelRequest ocrq{msg};
        push_sv(41, ocrq.orig_cl_ord_id());
        push_sv(11, ocrq.cl_ord_id());
        push_char(54, ocrq.side());
        push_sv(60, ocrq.transact_time());
        push_sv(55, ocrq.symbol());
        push_sv(1, ocrq.account());
    } else if (mt == "G") {  // OrderCancelReplaceRequest: B-11
        fixpp::v44::OrderCancelReplaceRequest ocrr{msg};
        push_sv(41, ocrr.orig_cl_ord_id());
        push_sv(11, ocrr.cl_ord_id());
        push_char(54, ocrr.side());
        push_sv(60, ocrr.transact_time());
        push_char(40, ocrr.ord_type());
        push_sv(55, ocrr.symbol());
        push_dec(38, ocrr.order_qty(&arena));
        push_dec(44, ocrr.price(&arena));
    }
    return out;
}

// T054 (FR-007/SC-002): the peer-to-fixpp `stage` order (inbound_business_count
// at the moment fromApp captures it) is exactly the six peer-originated steps
// in wire order -- B-02/B-04/B-06 (replies to B-01/B-03/B-05), then B-07/B-09/
// B-11 (which fixpp itself replies to with B-08/B-10/B-12, see the reply_step
// switch in fromApp below). Named here so the T054 typed-read assertion can
// look up each capture's script-declared intent by step_id.
std::string peer_step_id_for_stage(int stage)
{
    switch (stage) {
        case 0: return "B-02";
        case 1: return "B-04";
        case 2: return "B-06";
        case 3: return "B-07";
        case 4: return "B-09";
        case 5: return "B-11";
        default: return {};
    }
}

struct PendingSent {
    std::string script_step_id;
    std::vector<rb::FieldEntry> fields;
};

// T054: fixpp's OWN typed-accessor output for one peer-originated step,
// captured at read time so the TEST body can assert it against the peer's
// declared intent (data-model.md §2's `typed_reads`, previously written to
// the readback stream but never compared against anything -- FR-006's
// witness comparator ranges over the generic `fields` walk only).
struct TypedCapture {
    std::string step_id;
    std::vector<rb::TypedEntry> entries;
};

// The Application driving BOTH the passive (readback) and reactive-reply
// (B-08/B-10/B-12) halves of the conversation. `intent_by_step_originator`
// is set up before the session is registered and never mutated afterwards
// (read-only from the callback thread — single-exec confinement, session.hpp
// "fromApp(N+1) never begins before fromApp(N) returns").
class ConvApp : public Application {
public:
    fixpp::session::Engine* engine = nullptr;
    SessionId session_id;
    asio::any_io_executor exec;
    rb::Stream* stream = nullptr;
    std::map<std::pair<std::string, std::string>, intent::Message> const* intent_index = nullptr;

    std::optional<PendingSent> pending_sent;
    std::mutex occ_mu;
    std::map<std::pair<long long, std::string>, long long> occurrences;
    std::atomic<int> inbound_business_count{0};
    std::atomic<int> reactive_sends_failed{0};
    // T054: one entry per peer-originated business step, appended from
    // fromApp only (single-exec confined -- "fromApp(N+1) never begins
    // before fromApp(N) returns" -- so no lock is needed, matching how
    // `stream`/`pending_sent` are already written unlocked from the same
    // callback). Read back in the TEST body after the conversation settles.
    std::vector<TypedCapture> typed_captures;
    // B-08/B-10/B-12's runtime_generated OrderID(37)/ExecID(17) (spec.md's
    // own per-step notes: B-08 mints fresh; B-10 ECHOES B-08's OrderID; B-12
    // mints a second, independent fresh pair) — both only ever touched from
    // `exec` (fromApp / the posted reply lambdas), single-exec confined.
    std::atomic<int> id_mint_counter{0};
    std::string last_b08_order_id;

    long long next_occurrence(long long seq, std::string const& dir)
    {
        std::lock_guard<std::mutex> lk(occ_mu);
        return occurrences[{seq, dir}]++;
    }

    // Called synchronously right before Engine::send() for a fixpp-originated
    // message — the call site IS the builder-input capture (C-8).
    void arm_pending_sent(std::string step_id, std::vector<rb::FieldEntry> fields)
    {
        pending_sent = PendingSent{std::move(step_id), std::move(fields)};
    }

    fixpp::core::expected_t<void> toApp(MessageView<access_mode::Index> const& msg,
                                        SessionId const& /*id*/) override
    {
        if (stream != nullptr && pending_sent.has_value()) {
            PendingSent p = std::move(*pending_sent);
            pending_sent.reset();
            long long const seq = msg.msg_seq_num();
            long long const occ = next_occurrence(seq, std::string(rb::kDirectionFixppToPeer));
            stream->sent(std::string(msg.msg_type()), seq, rb::kDirectionFixppToPeer, occ,
                        p.script_step_id, std::move(p.fields));
        }
        return {};
    }

    fixpp::core::expected_t<void> fromApp(MessageView<access_mode::Index> const& msg,
                                          SessionId const& id) override
    {
        std::string const mt(msg.msg_type());
        long long const seq = msg.msg_seq_num();
        bool poss_dup = false;
        if (auto fv = msg.get(43); fv.has_value()) poss_dup = (fv->as_string() == "Y");

        // Hoisted above the readback/typed-capture block: `stage` names WHICH
        // peer-originated step this inbound message is (peer_step_id_for_stage),
        // needed for the T054 typed capture below, in addition to its existing
        // use for the reactive-reply decision.
        int const stage = inbound_business_count.fetch_add(1);

        if (stream != nullptr) {
            auto fields = conv::collect_body_fields(msg);
            auto typed = typed_reads_for(mt, msg);
            // T054: capture BEFORE the std::move into stream->readback() below
            // hands the vector away -- the TEST body asserts this copy against
            // the peer's declared intent (FR-007/SC-002).
            std::string const step_id = peer_step_id_for_stage(stage);
            if (!step_id.empty()) {
                typed_captures.push_back(TypedCapture{step_id, typed});
            }
            long long const occ = next_occurrence(seq, std::string(rb::kDirectionPeerToFixpp));
            stream->readback(mt, seq, rb::kDirectionPeerToFixpp, occ, poss_dup, std::move(fields),
                            std::move(typed));
        }

        std::string reply_step;
        if (stage == 3) reply_step = "B-08";       // reply to B-07
        else if (stage == 4) reply_step = "B-10";  // reply to B-09
        else if (stage == 5) reply_step = "B-12";  // reply to B-11
        if (reply_step.empty() || intent_index == nullptr) {
            return {};
        }

        auto it = intent_index->find({reply_step, "fixpp"});
        if (it == intent_index->end()) {
            ADD_FAILURE() << "no fixpp intent entry for reactive reply step " << reply_step;
            return {};
        }
        intent::Message const decl = it->second;  // copy: outlives the posted lambda safely

        // Off-strand hop (INV-7 pattern, test_business_message_interop.cpp
        // RespondingApp) before the re-entrant Engine::send().
        auto* eng = engine;
        auto sid = session_id;
        auto ex = exec;
        auto* self = this;
        asio::post(exec, [eng, sid, ex, self, decl]() mutable {
            std::vector<intent::FieldEntry> build_fields = decl.fields;
            if (decl.step_id == "B-08") {
                int const n = self->id_mint_counter.fetch_add(1) + 1;
                self->last_b08_order_id = "FXORD" + std::to_string(n);
                build_fields.push_back({"37", self->last_b08_order_id});
                build_fields.push_back({"17", "FXEXC" + std::to_string(n)});
            } else if (decl.step_id == "B-10") {
                build_fields.push_back({"37", self->last_b08_order_id});
            } else if (decl.step_id == "B-12") {
                int const n = self->id_mint_counter.fetch_add(1) + 1;
                build_fields.push_back({"37", "FXORD" + std::to_string(n)});
                build_fields.push_back({"17", "FXEXC" + std::to_string(n)});
            }

            std::vector<rb::FieldEntry> sent_fields;
            sent_fields.reserve(build_fields.size());
            for (auto const& f : build_fields) sent_fields.push_back({f.path, f.value});
            self->arm_pending_sent(decl.step_id, std::move(sent_fields));

            std::array<std::byte, 2048> buf{};
            auto body_r = conv::build_body_from_intent(buf, decl.msg_type, build_fields);
            if (!body_r.has_value()) {
                self->reactive_sends_failed.fetch_add(1);
                return;
            }
            asio::co_spawn(ex, eng->send(sid, *body_r),
                           [self](std::exception_ptr ep, fixpp::core::expected_t<void> r) {
                               if (ep || !r.has_value()) self->reactive_sends_failed.fetch_add(1);
                           });
        });
        return {};
    }
};

std::string run_dir_of(std::string const& readback_path)
{
    std::size_t const slash = readback_path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : readback_path.substr(0, slash);
}

}  // namespace

TEST(Conversation, Cell)
{
    // FR-023: the standard interop skip-with-reason convention (same macro
    // every other interop cell uses) — a bare ctest run with no counterparty
    // listening (the default in library CI, no shim involved) SKIPS here,
    // never reaching the hard-failure env checks below. Once a counterparty
    // IS listening this binary is being driven by the shim, and everything
    // past this point is data-model.md §1/§9's "absent is a hard failure,
    // never a skip" territory.
    INTEROP_REQUIRE_COUNTERPARTY("quickfix-cpp");

    // ── Run-identity env (data-model.md §1/§9) — hard failure, never a skip:
    // this binary exists ONLY to be launched as an 089 conversation cell. ──
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
    ASSERT_FALSE(arm.empty()) << "INTEROP_FIXPP_ARM absent";
    ASSERT_FALSE(combo.empty()) << "INTEROP_FIXPP_COMBO_ID absent";
    ASSERT_FALSE(script_path.empty()) << "INTEROP_FIXPP_SCRIPT_PATH absent";
    ASSERT_FALSE(script_digest_expected.empty()) << "INTEROP_FIXPP_SCRIPT_DIGEST absent";
    ASSERT_FALSE(readback_path.empty()) << "INTEROP_FIXPP_READBACK_PATH absent";
    ASSERT_FALSE(intent_path.empty()) << "INTEROP_FIXPP_INTENT_PATH absent";
    // Fail closed on an unrecognised/not-yet-implemented combo: this driver
    // hardcodes the step sequence below (FR-008d(a): no side may carry a YAML
    // parser), so silently running that sequence under a combo label this
    // driver does not actually implement would be a lying observable, not a
    // skip. Admits exactly the combos this driver implements so far — widen
    // this SET, never loosen it to an inequality (an inequality admits every
    // future not-yet-implemented combo too).
    ASSERT_TRUE(combo == "C1" || combo == "C2")
        << "combo " << combo << " is not implemented by this driver (only C1/C2 so far)";
    // C1 = fixpp INITIATOR vs QuickFIX-cpp acceptor; C2 = fixpp ACCEPTOR vs
    // QuickFIX-cpp initiator (spec.md § Conversation census, role x flavour
    // combinations). Every business/admin STEP's originator is combo-
    // independent (conversation_script.yaml: every step_id's
    // applicable_combos lists C1..C4 uniformly) -- only the TRANSPORT role
    // flips, so the driver below is otherwise unchanged between the two.
    Role const role = (combo == "C2") ? Role::fixpp_acceptor : Role::fixpp_initiator;

    std::string const actual_digest = sha256_hex_file(script_path);
    ASSERT_EQ(actual_digest, script_digest_expected) << "script_digest mismatch";

    std::vector<intent::Message> const messages = intent::parse_intent_file(intent_path);
    std::map<std::pair<std::string, std::string>, intent::Message> intent_index;
    for (auto const& m : messages) intent_index[{m.step_id, m.originator}] = m;

    auto prod = hp::production_dictionary_and_digest();

    char const* dir = hp::tls_fixture_dir();
    ASSERT_NE(dir, nullptr) << "FIXPP_TLS_FIXTURE_DIR unset";
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";
    auto const endpoint = hp::cell_endpoint(Counterparty::quickfix_cpp, role);
    ASSERT_TRUE(endpoint.has_value()) << "cell endpoint unresolved";

    auto app = std::make_shared<ConvApp>();
    fixpp::core::EngineConfig ecfg;
    ecfg.application = app;
    fixpp::interop::InteropEngineFixture fx{std::move(ecfg)};

    auto cfg = hp::make_session_config(role, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    cfg.dictionary = prod.dictionary;
    cfg.validate_inbound_messages = (arm == "validation-on");
    std::string const sender_id = cfg.sender_comp_id;
    std::string const target_id = cfg.target_comp_id;
    std::string const begin_string = cfg.begin_string;
    auto const id = SessionId::from_config(cfg);

    app->engine = &fx.engine();
    app->session_id = id;
    app->exec = fx.ioc().get_executor();
    app->intent_index = &intent_index;

    rb::Stream stream(readback_path);
    ASSERT_TRUE(stream.ok()) << "cannot open fixpp readback stream: " << readback_path;
    app->stream = &stream;

    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value()) << "register_session failed";
    fx.start();

    // ── A-LOGON ──────────────────────────────────────────────────────────────
    auto const reached = hp::drive_to_active(fx, id, 5s);
    ASSERT_EQ(reached, fsm_state::Active) << "session did not reach Active";

    auto sess = fx.engine().lookup(id);
    ASSERT_NE(sess, nullptr);

    bool const has_validator = sess->has_validator_for_test();
    stream.hello(run_id, cell_id, config, actual_digest, arm, has_validator, prod.dictionary_digest);
    // FR-011a / E-6: a validation-on arm MUST attest a live validator.
    EXPECT_EQ(has_validator, arm == "validation-on")
        << "has_validator (" << has_validator << ") disagrees with arm " << arm;

    // ── A-TESTREQ (peer TestRequest -> fixpp Heartbeat): pure session-layer,
    // no application code either side. Brief settle. ──────────────────────────
    fx.run_until([] { return false; }, 500ms);

    // ── A-GAPFILL: the peer (counterparty) bumps its own outbound seqnum and
    // sends a throwaway stimulus after answering A-TESTREQ; fixpp's engine
    // auto-detects the gap and auto-emits ResendRequest (session.cpp), which
    // the peer's engine auto-answers with SequenceReset-GapFill. Confirmed
    // (measured, not assumed) against QuickFIX-cpp with a standalone probe
    // before this driver was written — see the implementation report. No
    // application code on either side; wait for fixpp's own outbound counter
    // to reflect the auto-ResendRequest (Logon=1, A-TESTREQ Heartbeat=2,
    // ResendRequest=3) before A-REJECT claims the next slot. ────────────────
    bool const gapfill_advanced = fx.run_until(
        [&] { return sess->seqnum_mgr_test_access().peek_outbound() >= fixpp::session::seqnum_t{3}; },
        3s);
    EXPECT_TRUE(gapfill_advanced)
        << "A-GAPFILL: fixpp's automatic ResendRequest was not observed (outbound seq stalled at "
        << static_cast<std::uint32_t>(sess->seqnum_mgr_test_access().peek_outbound()) << ")";

    // ── A-REJECT: fixpp deliberately sends a malformed TestRequest via the
    // FIXPP_TEST_HOOKS seam (see file header for why Engine::send() does not
    // apply here). ─────────────────────────────────────────────────────────
    {
        auto send_fut = asio::co_spawn(
            fx.ioc().get_executor(),
            [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
                auto seq_r = co_await sess->seqnum_mgr_test_access().assign_outbound();
                if (!seq_r.has_value()) co_return std::unexpected(seq_r.error());
                std::array<std::byte, 512> buf{};
                std::vector<intent::FieldEntry> const reject_fields = {
                    {"112", "TR-ADMIN-0002"}, {"55", "OUT-OF-CONTEXT"}};
                auto frame_r = conv::build_frame_via_writer(buf, "1", *seq_r, sender_id, target_id,
                                                            begin_string, now_utc_ms(), reject_fields);
                if (!frame_r.has_value()) co_return std::unexpected(frame_r.error());
                co_return co_await sess->store_then_emit_test_access(*seq_r, *frame_r);
            }(),
            asio::use_future);
        fx.run_until([&] { return send_fut.wait_for(0ms) == std::future_status::ready; }, 3s);
        ASSERT_EQ(send_fut.wait_for(0ms), std::future_status::ready)
            << "A-REJECT: sending the malformed TestRequest did not complete within 3s";
        auto const r = send_fut.get();
        EXPECT_TRUE(r.has_value()) << "A-REJECT: store_then_emit_test_access failed";
    }
    // Let the peer's Reject arrive; the session must survive it (measured:
    // QuickFIX-cpp replies Reject(35=3) rather than disconnecting — see the
    // implementation report).
    fx.run_until([] { return false; }, 800ms);
    EXPECT_EQ(sess->state(), fsm_state::Active)
        << "session did not survive A-REJECT's malformed TestRequest exchange";

    // ── Business steps B-01/B-03/B-05: fixpp-originated, from intent ────────
    auto send_fixpp_business = [&](std::string const& step_id) -> bool {
        auto it = intent_index.find({step_id, "fixpp"});
        if (it == intent_index.end()) {
            ADD_FAILURE() << "no fixpp intent entry for " << step_id;
            return false;
        }
        intent::Message const& decl = it->second;
        std::vector<rb::FieldEntry> sent_fields;
        sent_fields.reserve(decl.fields.size());
        for (auto const& f : decl.fields) sent_fields.push_back({f.path, f.value});
        // The peer's readback genuinely reports the NoXxx COUNT field (it is
        // on the wire) -- the sent record must carry it too, or FR-006 sees
        // a `spurious` mismatch on every group-bearing step (B-01).
        for (auto const& f : conv::derive_group_count_fields(decl.fields)) {
            sent_fields.push_back({f.path, f.value});
        }
        app->arm_pending_sent(step_id, std::move(sent_fields));

        std::array<std::byte, 2048> buf{};
        auto body_r = conv::build_body_from_intent(buf, decl.msg_type, decl.fields);
        if (!body_r.has_value()) {
            ADD_FAILURE() << "build_body_from_intent failed for " << step_id
                          << "; error=" << static_cast<int>(body_r.error());
            return false;
        }
        auto fut = asio::co_spawn(fx.ioc().get_executor(), fx.engine().send(id, *body_r),
                                  asio::use_future);
        fx.run_until([&] { return fut.wait_for(0ms) == std::future_status::ready; }, 3s);
        if (fut.wait_for(0ms) != std::future_status::ready) {
            ADD_FAILURE() << "Engine::send timed out for " << step_id;
            return false;
        }
        auto const r = fut.get();
        if (!r.has_value()) {
            ADD_FAILURE() << "Engine::send failed for " << step_id
                          << "; error=" << static_cast<int>(r.error());
            return false;
        }
        return true;
    };

    // B-05: fixpp #418 -- body_builder cannot carry EncodedText(355)'s 0xff
    // byte (C-11), so this ONE step is sent as a hand-built frame through the
    // FIXPP_TEST_HOOKS seam A-REJECT already uses (user decision 2026-09-11;
    // spec.md § Conversation census → the B-05 bullet). Its `sent` record
    // still comes from the intent file, never from the hand-built frame
    // (C-8) -- and since `store_then_emit_test_access` bypasses the normal
    // Engine::send()/toApp flow entirely, that record is written HERE
    // directly rather than via ConvApp::arm_pending_sent/toApp. ⚠️ What this
    // route does NOT exercise: fixpp's own builder — see
    // conv_wire.hpp::build_frame_via_writer's header comment.
    auto send_b05_via_test_hook = [&]() -> bool {
        auto it = intent_index.find({"B-05", "fixpp"});
        if (it == intent_index.end()) {
            ADD_FAILURE() << "no fixpp intent entry for B-05";
            return false;
        }
        intent::Message const& decl = it->second;

        fixpp::session::seqnum_t assigned_seq{};
        auto send_fut = asio::co_spawn(
            fx.ioc().get_executor(),
            [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
                auto seq_r = co_await sess->seqnum_mgr_test_access().assign_outbound();
                if (!seq_r.has_value()) co_return std::unexpected(seq_r.error());
                assigned_seq = *seq_r;
                std::array<std::byte, 512> buf{};
                auto frame_r = conv::build_frame_via_writer(buf, decl.msg_type, *seq_r, sender_id,
                                                            target_id, begin_string, now_utc_ms(),
                                                            decl.fields);
                if (!frame_r.has_value()) co_return std::unexpected(frame_r.error());
                co_return co_await sess->store_then_emit_test_access(*seq_r, *frame_r);
            }(),
            asio::use_future);
        fx.run_until([&] { return send_fut.wait_for(0ms) == std::future_status::ready; }, 3s);
        if (send_fut.wait_for(0ms) != std::future_status::ready) {
            ADD_FAILURE() << "B-05: hand-built-frame send timed out";
            return false;
        }
        auto const r = send_fut.get();
        if (!r.has_value()) {
            ADD_FAILURE() << "B-05: store_then_emit_test_access failed; error="
                          << static_cast<int>(r.error());
            return false;
        }

        std::vector<rb::FieldEntry> sent_fields;
        sent_fields.reserve(decl.fields.size());
        for (auto const& f : decl.fields) sent_fields.push_back({f.path, f.value});
        long long const seq_ll = static_cast<long long>(static_cast<std::uint32_t>(assigned_seq));
        long long const occ = app->next_occurrence(seq_ll, std::string(rb::kDirectionFixppToPeer));
        stream.sent(decl.msg_type, seq_ll, rb::kDirectionFixppToPeer, occ, "B-05",
                   std::move(sent_fields));
        return true;
    };

    ASSERT_TRUE(send_fixpp_business("B-01"));
    ASSERT_TRUE(fx.run_until([&] { return app->inbound_business_count.load() >= 1; }, 5s))
        << "no reply to B-01 (B-02) within 5s";

    ASSERT_TRUE(send_fixpp_business("B-03"));
    ASSERT_TRUE(fx.run_until([&] { return app->inbound_business_count.load() >= 2; }, 5s))
        << "no reply to B-03 (B-04) within 5s";

    ASSERT_TRUE(send_b05_via_test_hook());
    ASSERT_TRUE(fx.run_until([&] { return app->inbound_business_count.load() >= 3; }, 5s))
        << "no reply to B-05 (B-06) within 5s";

    // ── B-07..B-12: peer originates, fixpp replies reactively (ConvApp::fromApp) ──
    ASSERT_TRUE(fx.run_until([&] { return app->inbound_business_count.load() >= 6; }, 8s))
        << "conversation did not complete; inbound_business_count="
        << app->inbound_business_count.load();
    EXPECT_EQ(app->reactive_sends_failed.load(), 0) << "one or more reactive replies failed to send";

    // Settle for the peer's own last readback/transcript writes.
    fx.run_until([] { return false; }, 300ms);

    stream.terminal("completed", run_id, cell_id, config, actual_digest);

    // ── Witnesses (data-model.md §4) ─────────────────────────────────────────
    std::string const run_dir = run_dir_of(readback_path);
    std::string const cp_path = run_dir + "/counterparty-readback.jsonl";
    auto const fixpp_records = rb::parse_stream(readback_path);
    auto const cp_records = rb::parse_stream(cp_path);

    rb::WitnessIdentity wid;
    wid.run_id = run_id;
    wid.combo_id = combo;
    wid.cell_id = cell_id;
    wid.config = config;
    wid.arm = arm;
    wid.kind = "conformance";
    wid.authoritative = true;

    std::string const dict_path = env_or_empty("FIXPP_FIX44_DICT_XML");
    auto is_decimal = rb::make_fix44_decimal_resolver(dict_path);
    auto const rows = rb::compare_streams(fixpp_records, cp_records, wid, is_decimal);
    EXPECT_TRUE(rb::write_witness_rows(run_dir + "/witnesses.jsonl", rows))
        << "failed to write witnesses.jsonl";

    for (auto const& row : rows) {
        EXPECT_EQ(row.verdict, "pass")
            << "witness " << row.witness_id << " (" << row.msg_type << ") FAILED";
        for (auto const& m : row.mismatch) {
            ADD_FAILURE() << "  " << row.witness_id << " " << m.cls << " path=" << m.path
                          << " sent=" << m.sent_value << " readback=" << m.readback_value;
        }
    }
    EXPECT_EQ(rows.size(), 12u) << "expected 12 business-step witness rows (12 census keys for combo "
                                << combo << ", spec.md § Conversation census)";

    // ── T054: fixpp's typed-read tier must return the peer's DECLARED values
    // (FR-007/SC-002) ────────────────────────────────────────────────────────
    // `typed_reads_for()` above (fromApp) captures fixpp's OWN generated
    // typed-accessor output for every peer-to-fixpp business step, and until
    // now nothing compared it against anything: witness_comparator.hpp's
    // FR-006 comparator ranges over the generic `fields` body walk only,
    // never `typed_reads` (that field is written to the stream as
    // descriptive evidence, per its own header comment). Assert here,
    // explicitly, that every CAPTURED typed value equals the script's
    // declared value for that (step, tag) -- canonicalized the SAME way
    // typed_reads_for() itself canonicalizes (rb::canonical_typed_value),
    // so a PRICE/QTY spelling difference ("190.50" vs "190.5") cannot read
    // as a false mismatch.
    //
    // Iterates over what was CAPTURED, never the reverse: typed_reads_for()
    // is explicitly best-effort (its own header comment — "a field the
    // message declares but this cell's script does not exercise is simply
    // omitted"), and several captured tags (ExecID(17)/OrderID(37) on
    // B-02/B-04/B-06, the peer engine's own free-form IDs) have no
    // script-declared counterpart at all — skipped, not asserted absent.
    // EXPECT_GT below guards the OTHER direction: an empty capture for a
    // step would otherwise satisfy an empty for-loop vacuously.
    ASSERT_FALSE(app->typed_captures.empty())
        << "T054: zero typed-read captures for the whole conversation -- "
           "the assertion below would be vacuous";
    for (auto const& cap : app->typed_captures) {
        auto it = intent_index.find({cap.step_id, "peer"});
        ASSERT_NE(it, intent_index.end())
            << "T054: no peer intent entry declared for step " << cap.step_id;
        EXPECT_GT(cap.entries.size(), 0u)
            << "T054: zero typed fields captured for step " << cap.step_id;
        for (auto const& te : cap.entries) {
            auto declared = std::find_if(
                it->second.fields.begin(), it->second.fields.end(),
                [&](intent::FieldEntry const& f) { return f.path == te.path; });
            if (declared == it->second.fields.end()) {
                continue;  // no script-declared counterpart (e.g. peer-engine-minted ID) --
                           // nothing to compare against, not an assertable absence.
            }
            std::string const expected = rb::canonical_typed_value(te.fix_type, declared->value);
            EXPECT_EQ(te.value, expected)
                << "T054: step " << cap.step_id << " tag " << te.path
                << " typed-read=" << te.value << " declared=" << expected;
        }
    }

    hp::expect_graceful_stop(fx);
}
