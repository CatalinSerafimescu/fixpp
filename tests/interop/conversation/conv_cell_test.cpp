// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/conversation/conv_cell_test.cpp — 089 Phase 5 (US3): the
// combo-neutral conversation driver. ONE gtest binary/TEST body for every
// combo C1-C4 (C1/C2 -- fixpp vs QuickFIX-cpp -- and C3/C4 -- fixpp vs
// QuickFIX-J, see qfj_combo_probe below), selected/configured entirely by
// the shim's environment
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
// T060: QuickFIX-J-only arms, declared EXPLICITLY rather than silently
// skipped. L-021-3 records that QuickFIX-cpp's Session::send() strips
// PossDupFlag(43)/OrigSendingTime(122) unconditionally and exposes no
// public injection knob, so any arm needing PEER-ORIGINATED hostile/replay
// input is QuickFIX-J-only -- the reason the PD-QFj-* cells (pre-089) are
// QF-J-only, and it is ALSO why this census's own "declared_inapplicable"
// section (census.yaml) excludes "peer-originated replay (any step)" from
// ALL FOUR combos of THIS feature: a peer-originated replay is out of
// scope for 089 on every engine, not conditionally missing on QuickFIX-cpp.
// A-RESEND (this feature's one resend/replay admin step) is the opposite
// direction -- a FIXPP-originated replay the PEER receives -- and is
// QF-J-only for a DIFFERENT, narrower reason: the census declares it
// applicable_combos: [C3, C4] only (not a QuickFIX-cpp injection-knob gap
// at all); the qfj_combo_probe-gated blocks below are where that
// declaration is enforced in code, never a bare GTEST_SKIP with no reason
// (there is nothing to skip -- C1/C2 simply never enter those blocks, by
// the combo gate above, and every A-RESEND-dependent assertion states its
// own reason inline -- see describe_a_resend_rejection()).
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
#include <functional>
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
#include <fixpp/session/memory_store_factory.hpp>
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
    // A-RESEND (C3/C4 only): B-01's fixpp-outbound seq_num, captured in
    // toApp() below at the moment B-01's own PendingSent is consumed. The
    // TEST body needs this to write B-01 occurrence 1's `sent` record once
    // it observes the peer's replay-readback land (C-8: the record must
    // still be builder-derived from intent, not re-read from the replayed
    // frame -- this seq_num is header identity, not a body field).
    std::atomic<long long> b01_seq{-1};

    // T061a fix: `app->stream` is wired BEFORE the Logon exchange (so
    // fromApp/toApp can observe B-01..B-12), but `stream.hello()` is written
    // by the TEST body only AFTER drive_to_active() returns. `run_until()`
    // pumps the io_context in wall-clock SLICES and checks its ready
    // predicate only BETWEEN slices -- so a peer-initiated admin message
    // that arrives batched with (or immediately after) the Logon exchange
    // (A-TESTREQ is engine-automatic and peer-scheduled, not driven by this
    // test body) can be processed by fromAdmin/toAdmin and written to the
    // stream WITHIN THE SAME SLICE drive_to_active() pumps to reach Active
    // -- before the test body's next line calls stream.hello(). Every
    // stream write goes through write_or_defer() below, which buffers a
    // write until mark_hello_written() (called right after stream.hello())
    // flushes it -- preserving readback-jsonl.md's "hello is the first
    // line, emitted before any message is processed" for every record kind
    // this driver writes, not just the ones the TEST body itself sequences.
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
        for (auto& fn : pending_before_hello) {
            fn();
        }
        pending_before_hello.clear();
        hello_written = true;
    }

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
            if (p.script_step_id == "B-01") {
                b01_seq.store(seq);
            }
            long long const occ = next_occurrence(seq, std::string(rb::kDirectionFixppToPeer));
            rb::Stream* s = stream;
            write_or_defer([s, mt = std::string(msg.msg_type()), seq, occ,
                            step_id = p.script_step_id, fields = std::move(p.fields)]() mutable {
                s->sent(mt, seq, rb::kDirectionFixppToPeer, occ, step_id, std::move(fields));
            });
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
            // data-model §13/T061a: the disposition ordinal is the SAME
            // arrival counter the readback record above just consumed — one
            // shared counter serves both, never a second independent count.
            rb::Stream* s = stream;
            write_or_defer([s, mt, seq, occ]() { s->disposition(mt, seq, rb::kDirectionPeerToFixpp, occ,
                                                                  "accepted"); });
            write_or_defer([s, mt, seq, occ, poss_dup, fields = std::move(fields),
                            typed = std::move(typed)]() mutable {
                s->readback(mt, seq, rb::kDirectionPeerToFixpp, occ, poss_dup, std::move(fields),
                            std::move(typed));
            });
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

    // data-model §13/T061a: an ADMIN arrival fixpp's session DELIVERED (any
    // message reaching this callback was accepted — the validator gate that
    // could have rejected it runs BEFORE FSM delivery, session.hpp/session.cpp
    // facts in the task brief). Most admin traffic in this conversation
    // (A-LOGON/A-TESTREQ's Heartbeat reply/A-GAPFILL's SequenceReset) never
    // has a peer `sent` record to join (both counterparties' writers only
    // call stream->sent() from toApp — application messages — never from
    // toAdmin), so these dispositions legitimately "enter no set" per
    // data-model §13's own text; they are still written because §13 defines
    // `accepted` at fromApp/fromAdmin delivery, not at "joins something".
    fixpp::core::expected_t<void> fromAdmin(MessageView<access_mode::Index> const& msg,
                                            SessionId const& /*id*/) override
    {
        if (stream != nullptr) {
            long long const seq = msg.msg_seq_num();
            long long const occ = next_occurrence(seq, std::string(rb::kDirectionPeerToFixpp));
            rb::Stream* s = stream;
            write_or_defer([s, mt = std::string(msg.msg_type()), seq, occ]() {
                s->disposition(mt, seq, rb::kDirectionPeerToFixpp, occ, "accepted");
            });
        }
        return {};
    }

    // data-model §13/T061a: the `rejected` disposition side — observed as
    // fixpp's OWN outbound session Reject(35=3), inspected here (toAdmin is
    // inspect-only; the message is always sent regardless of this override's
    // body, FR-008). RefSeqNum(45) names the rejected inbound arrival, which
    // never reached fromApp/fromAdmin above (the validator gate runs BEFORE
    // FSM delivery) -- so this call site and the two above are mutually
    // exclusive per arrival, and next_occurrence's shared counter assigns the
    // correct ordinal whichever of the three fires.
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
        long long const occ = next_occurrence(ref_seq, std::string(rb::kDirectionPeerToFixpp));
        rb::Stream* s = stream;
        write_or_defer([s, ref_msg_type, ref_seq, occ, reject]() {
            s->disposition(ref_msg_type, ref_seq, rb::kDirectionPeerToFixpp, occ, "rejected", reject);
        });
    }
};

std::string run_dir_of(std::string const& readback_path)
{
    std::size_t const slash = readback_path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : readback_path.substr(0, slash);
}

// A-RESEND (C3/C4) is currently expected to fail: fixpp #419 -- both
// build_sequence_reset_gapfill (session-admin GapFill emitter) and
// build_replay_frame (the ResendRequest-answer replay path) append
// PossDupFlag(43)/OrigSendingTime(122) AFTER every body field instead of in
// standard-header position, which QuickFIX-J's UseDataDictionary=Y field-
// order validation rejects (measured: Reject(35=3) 373=14 "Tag specified
// out of required order, field=43"). Scans the counterparty's plaintext
// transcript (a sibling of readback_path -- the same file the C++/Java
// counterparties both write "OUT <msgtype> <fix-with-pipes>" lines to) for
// that exact signature and, if found, names the known cause explicitly
// rather than leaving a bare "replay not observed" diagnostic -- per the
// coordinator's instruction that C3/C4 must fail for THIS reason, visibly,
// never silently. Returns an empty string if the signature is not found
// (e.g. #419 has since landed and something else is wrong instead).
std::string describe_a_resend_rejection(std::string const& run_dir)
{
    std::ifstream f(run_dir + "/counterparty-transcript.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("35=3") != std::string::npos &&
            line.find("Tag specified out of required order, field=43") != std::string::npos) {
            return "peer rejected fixpp's B-01 replay with Reject(35=3) 373=14 "
                   "(\"Tag specified out of required order\") field=43 -- known cause: "
                   "fixpp #419 (PossDupFlag(43)/OrigSendingTime(122) appended after every "
                   "body field in build_sequence_reset_gapfill/build_replay_frame instead "
                   "of standard-header position; QuickFIX-J's UseDataDictionary=Y field-"
                   "order validation rejects it). A-RESEND stays red until #419 lands. "
                   "Wire line: " + line;
        }
    }
    return {};
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
    // FR-023: the standard interop skip-with-reason convention -- a bare
    // ctest run with no counterparty listening SKIPS here, never reaching
    // the hard-failure env checks below. Which counterparty token to probe
    // depends on the combo (C1/C2 -> quickfix-cpp, C3/C4 -> quickfix-j), so
    // `combo` must be read (bare, no assertion yet) before the skip gate.
    std::string const combo = env_or_empty("INTEROP_FIXPP_COMBO_ID");
    bool const qfj_combo_probe = (combo == "C3" || combo == "C4");
    if (qfj_combo_probe) {
        INTEROP_REQUIRE_COUNTERPARTY("quickfix-j");
    } else {
        INTEROP_REQUIRE_COUNTERPARTY("quickfix-cpp");
    }

    // ── Run-identity env (data-model.md §1/§9) — hard failure, never a skip:
    // this binary exists ONLY to be launched as an 089 conversation cell. ──
    std::string const run_id = env_or_empty("INTEROP_FIXPP_RUN_ID");
    std::string const cell_id = env_or_empty("INTEROP_FIXPP_CELL_ID");
    std::string const config = env_or_empty("INTEROP_FIXPP_CONFIG");
    std::string const arm = env_or_empty("INTEROP_FIXPP_ARM");
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
    ASSERT_TRUE(combo == "C1" || combo == "C2" || combo == "C3" || combo == "C4")
        << "combo " << combo << " is not implemented by this driver (only C1-C4)";
    // C1 = fixpp INITIATOR vs QuickFIX-cpp acceptor; C2 = fixpp ACCEPTOR vs
    // QuickFIX-cpp initiator; C3 = fixpp INITIATOR vs QuickFIX-J acceptor;
    // C4 = fixpp ACCEPTOR vs QuickFIX-J initiator (spec.md § Conversation
    // census, role x flavour combinations). Every business/admin STEP's
    // originator is combo-independent (conversation_script.yaml: every
    // step_id's applicable_combos lists C1..C4 uniformly) -- only the
    // TRANSPORT role and the COUNTERPARTY engine flip, so the driver below
    // is otherwise unchanged across all four.
    bool const acceptor_combo = (combo == "C2" || combo == "C4");
    Role const role = acceptor_combo ? Role::fixpp_acceptor : Role::fixpp_initiator;
    Counterparty const counterparty =
        qfj_combo_probe ? Counterparty::quickfix_j : Counterparty::quickfix_cpp;

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
    auto const endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value()) << "cell endpoint unresolved";

    auto app = std::make_shared<ConvApp>();
    fixpp::core::EngineConfig ecfg;
    ecfg.application = app;
    fixpp::interop::InteropEngineFixture fx{std::move(ecfg)};

    auto cfg = hp::make_session_config(role, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    cfg.dictionary = prod.dictionary;
    cfg.validate_inbound_messages = (arm == "validation-on");
    // A-RESEND / A-GAPFILL: give fixpp a persistent outbound store so it can
    // REPLAY a stored application body (35=D, 43=Y) in answer to a
    // ResendRequest, rather than collapse every range to a SequenceReset-
    // GapFill (a storeless session cannot replay app bodies -- Session::
    // replay_outbound_range_'s `if (!store_ || our_last == 0 ...)` early
    // branch, session.cpp ~5387). Same precedent as
    // hp_fix44_recovery_outbound_answer_test.cpp (~line 268): unbounded
    // policy, exempt from the bounded-store DoS construction guard that
    // would otherwise abort session open under the engine's default
    // max_store_memory_bytes. Test-only store; applies to every combo (the
    // guard/policy choice is not combo-specific), so this is NOT gated on
    // qfj_combo_probe.
    cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
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
    // Flushes any admin-arrival disposition ConvApp buffered while
    // drive_to_active() was pumping (T061a fix, ConvApp::write_or_defer's own
    // header comment) -- MUST run immediately after stream.hello() so no
    // buffered record's ordinal can ever precede the hello line on disk.
    app->mark_hello_written();
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
    // both QuickFIX-cpp and QuickFIX-J reply Reject(35=3) rather than
    // disconnecting -- see the implementation report).
    fx.run_until([] { return false; }, 800ms);
    EXPECT_EQ(sess->state(), fsm_state::Active)
        << "session did not survive A-REJECT's malformed TestRequest exchange";

    // T058: forced-miss arm. A bare "session survived" check is satisfied by
    // a peer that silently IGNORES the malformed TestRequest -- indistinguishable
    // from one that actually validated and rejected it. Read the peer's own
    // transcript for the SPECIFIC Reject(35=3) signature this exchange
    // produces (RefMsgType=1/TestRequest, SessionRejectReason=2/"Tag not
    // defined for this message type"); tolerate a disconnect as the
    // documented alternative outcome (KNOWN-LIMITATIONS.md:87-106: only
    // QuickFIX-J 3.0.1 is CONFIRMED to emit Reject(35=3) on this pinned
    // input -- measured here to hold for QuickFIX-cpp too, but the task's
    // own tolerance is kept so a counterparty rebuild that changes this
    // behavior does not spuriously fail this cell). Neither outcome is a
    // "fidelity pass" (T058) -- this assertion exists ONLY to prove the cell
    // WOULD fail if the peer did neither, which today it could not.
    {
        std::ifstream reject_transcript(run_dir_of(readback_path) + "/counterparty-transcript.txt");
        std::string reject_line;
        bool peer_rejected = false;
        while (std::getline(reject_transcript, reject_line)) {
            if (reject_line.find("35=3") != std::string::npos &&
                reject_line.find("372=1") != std::string::npos &&
                reject_line.find("373=2") != std::string::npos) {
                peer_rejected = true;
                break;
            }
        }
        bool const peer_disconnected = (sess->state() != fsm_state::Active);
        EXPECT_TRUE(peer_rejected || peer_disconnected)
            << "T058: peer neither rejected A-REJECT's malformed TestRequest "
               "(no Reject(35=3,372=1,373=2) line in its transcript) nor disconnected -- "
               "it silently tolerated hostile input it should have refused";
    }

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

    // ── A-RESEND (C3/C4 only, script step 13): the peer (QuickFIX-J) sends a
    // ResendRequest covering B-01's fixpp-outbound seq once the whole
    // conversation settles (InteropCounterparty.java's induceResendForB01,
    // fired at its own stage 5 -- see its header comment for why not
    // immediately after B-08 despite depends_on: ['B-08']). fixpp's engine
    // answers via replay_outbound_range_ -- confirmed by direct source
    // reading NOT to invoke Application::toApp (session.cpp) -- so B-01
    // occurrence 1's `sent` record cannot come from the usual toApp seam and
    // is written HERE directly, mirroring B-05's hand-built-frame route.
    // C-8: the fields are the SAME declared intent as occurrence 0 (a replay
    // resends the identical message), never re-read from anything; the
    // occurrence number comes from the SAME shared counter occurrence 0 used
    // (app->next_occurrence), so the two can never disagree by construction.
    if (qfj_combo_probe) {
        ASSERT_GE(app->b01_seq.load(), 0) << "A-RESEND: B-01's fixpp-outbound seq_num was never captured";
        std::string const run_dir_early = run_dir_of(readback_path);
        std::string const cp_path_early = run_dir_early + "/counterparty-readback.jsonl";
        bool replay_observed = fx.run_until(
            [&] {
                auto const cp_records_poll = rb::parse_stream(cp_path_early);
                for (auto const& r : cp_records_poll) {
                    if (r.kind == rb::ParsedRecord::Kind::Readback && r.msg_type == "D" &&
                        r.direction == std::string(rb::kDirectionFixppToPeer) && r.occurrence == 1) {
                        return true;
                    }
                }
                return false;
            },
            5s);
        if (!replay_observed) {
            std::string const known_cause = describe_a_resend_rejection(run_dir_early);
            EXPECT_TRUE(replay_observed)
                << "A-RESEND: no B-01 occurrence 1 readback observed on the peer within 5s"
                << (known_cause.empty()
                        ? std::string(" -- cause not the known #419 signature; investigate")
                        : (" -- " + known_cause));
        }
        if (replay_observed) {
            auto it = intent_index.find({"B-01", "fixpp"});
            ASSERT_NE(it, intent_index.end()) << "no fixpp intent entry for B-01";
            intent::Message const& decl = it->second;
            std::vector<rb::FieldEntry> sent_fields;
            sent_fields.reserve(decl.fields.size());
            for (auto const& f : decl.fields) sent_fields.push_back({f.path, f.value});
            for (auto const& f : conv::derive_group_count_fields(decl.fields)) {
                sent_fields.push_back({f.path, f.value});
            }
            long long const seq = app->b01_seq.load();
            long long const occ = app->next_occurrence(seq, std::string(rb::kDirectionFixppToPeer));
            stream.sent("D", seq, rb::kDirectionFixppToPeer, occ, "B-01", std::move(sent_fields));
        }
        // Let the peer write its own terminal-adjacent state before proceeding.
        fx.run_until([] { return false; }, 200ms);
    }

    stream.terminal("completed", run_id, cell_id, config, actual_digest);

    // ── Witnesses (data-model.md §4) ─────────────────────────────────────────
    std::string const run_dir = run_dir_of(readback_path);
    std::string const cp_path = run_dir + "/counterparty-readback.jsonl";
    auto const fixpp_records = rb::parse_stream(readback_path);
    auto const cp_records = rb::parse_stream(cp_path);

    // ── T057 / C-11 (spec.md § Conversation census, B-05's own note): assert
    // EXPLICITLY, not just implicitly via the generic FR-006 witness pass
    // below, that B-05's EncodedText(355) 0xff byte survives QuickFIX-J's
    // live decode bit-for-bit -- `value_b64` for path 355 must equal the
    // base64 of the declared wire bytes. ParsedRecord::fields already
    // base64-decodes `value_b64` to raw bytes (witness_comparator.hpp), so
    // this is a direct byte-for-byte comparison, not a re-encode-and-compare.
    // The ISO-8859-1 charset in effect (charsetRefusalDiagnostic(), T017) is
    // what makes this bijective; see the RED proof in the implementation
    // report (INTEROP_CP_TEST_FORCE_CHARSET, a test-only startup override).
    if (qfj_combo_probe) {
        auto it = intent_index.find({"B-05", "fixpp"});
        ASSERT_NE(it, intent_index.end()) << "no fixpp intent entry for B-05";
        auto field_it = std::find_if(it->second.fields.begin(), it->second.fields.end(),
                                     [](intent::FieldEntry const& f) { return f.path == "355"; });
        ASSERT_NE(field_it, it->second.fields.end()) << "T057: B-05 declares no path 355 field";
        std::string const expected_bytes = field_it->value;

        bool found_355 = false;
        for (auto const& r : cp_records) {
            if (r.kind != rb::ParsedRecord::Kind::Readback || r.msg_type != "G" ||
                r.direction != std::string(rb::kDirectionFixppToPeer)) {
                continue;
            }
            auto fe = std::find_if(r.fields.begin(), r.fields.end(),
                                   [](rb::FieldEntry const& f) { return f.path == "355"; });
            if (fe == r.fields.end()) continue;
            found_355 = true;
            EXPECT_EQ(fe->value, expected_bytes)
                << "T057/C-11: peer's decoded EncodedText(355) does not byte-match the "
                   "declared wire bytes -- the 0xff byte did not survive QuickFIX-J's "
                   "live decode";
        }
        EXPECT_TRUE(found_355) << "T057: no peer readback record carries path 355 (B-05 never "
                                  "reached the peer, or the generic body walk dropped it)";
    }

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
    // census.yaml: 12 business_steps rows, each with occurrence {0} on
    // C1/C2 -- 12 keys. On C3/C4, B-01 additionally declares occurrence 1
    // (A-RESEND's replay, "declared_inapplicable" excludes it on C1/C2 only)
    // -- 13 keys. Derived from census.yaml, not an independent literal.
    std::size_t const expected_rows = qfj_combo_probe ? 13u : 12u;
    std::string const known_cause_tail =
        qfj_combo_probe ? [&] {
            std::string const c = describe_a_resend_rejection(run_dir);
            return c.empty() ? std::string() : (" -- " + c);
        }()
                        : std::string();
    EXPECT_EQ(rows.size(), expected_rows)
        << "expected " << expected_rows << " business-step witness rows (census.yaml keys for combo "
        << combo << ", spec.md § Conversation census)" << known_cause_tail;
    if (qfj_combo_probe) {
        // A-RESEND's whole point is B-01 occurrence 1 (spec.md's declared_
        // inapplicable note); rows.size()==13 alone is satisfied by ANY 13th
        // row (e.g. a duplicate elsewhere), so assert the specific key too.
        bool found_b01_occ1 = false;
        for (auto const& row : rows) {
            if (row.script_step_id == "B-01" && row.occurrence == 1) {
                found_b01_occ1 = true;
                break;
            }
        }
        EXPECT_TRUE(found_b01_occ1)
            << "A-RESEND: no witness row for B-01 occurrence 1 (the PossDup replay)"
            << known_cause_tail;
    }

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
