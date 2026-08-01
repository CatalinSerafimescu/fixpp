// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/capi/capi_commit_group_bench.cpp
//
// 083-group-delimiter-resolution T065 — FR-022 path 3 (C-ABI commit).
// `[const §VIII.3]`: benchmark in the same PR as the change.
//
// ── What 083 added to this path ─────────────────────────────────────────────
// `fixpp_msg_commit` runs `validate_group_grammar` over the message being
// built. Before 083 that check resolved the delimiter from the dictionary's
// global first-seen store; now it resolves `(msg_type, parent_path, no_tag)`
// and MAINTAINS the parent path across its own recursion — so the added work
// is per group instance, executed on every commit of a group-bearing message.
//
// A second, DIFFERENT cost was added at a different place in the lifecycle:
// the session now materializes its `table_view` ONCE at open (D-13) and caches
// it on the session handle, instead of the commit path reaching for one. That
// is session-open cost, not per-message cost, and the two must not be reported
// as one number — a per-message row that silently included a `as_table_view()`
// would read as a catastrophic commit regression, and a session-open row folded
// into an amortized per-message figure would hide it. Hence two separate
// benchmark families below.
//
// ── Cases ───────────────────────────────────────────────────────────────────
//   Commit_NoGroup    — control. No group, so `validate_group_grammar` has
//                       nothing to descend and the delimiter store is never
//                       consulted. Guards against the lookup leaking into the
//                       common commit path.
//   Commit_Group1     — one outer instance.
//   Commit_Group8     — eight outer instances of the same shape, so the
//                       PER-INSTANCE term is separable from the fixed
//                       per-commit term by comparing the two rows.
//   Commit_Nested     — an instance carrying a NESTED group, which is what
//                       exercises the path MAINTENANCE (`ctx.pushed(no_tag)`)
//                       rather than just a root-context lookup.
//   SessionOpen       — the once-per-session `as_table_view()`. Reported
//                       separately and deliberately NOT amortized.
//
// The dictionary is a small inline one loaded through the real C-ABI setter,
// so the message types and groups are exactly the shapes named above and the
// numbers are not perturbed by an unrelated 25k-context dictionary. The
// session-open row uses the SAME dictionary, so its figure is a floor for a
// real dictionary rather than a claim about one — stated rather than implied.
//
// Self-contained: this bench does NOT include tests/capi/capi_loopback_support
// .hpp, which pulls in gtest. It reproduces only the two documented seams it
// needs (L-050-1 dictionary handle, L-050-5 endpoint) directly.
//
// Baseline: bench/baselines/capi/capi_commit_group_bench.json.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"

#include "fixpp/dict/dictionary.hpp"
#include "fixpp/dict/xml_loader.hpp"
#include "fixpp/transport/endpoint.hpp"

namespace {

// 'G' — GroupMsg: NoOuter(100) delimited by OuterField(201), with a nested
//       NoInner(200) delimited by InnerField(202).
// 'P' — PlainMsg: no groups at all (the commit-path control).
constexpr std::string_view kBenchXml =
    R"(<fix type='FIX' major='4' minor='2' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='34' name='MsgSeqNum' type='INT'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='49' name='SenderCompID' type='STRING'/>)"
    R"(<field number='52' name='SendingTime' type='UTCTIMESTAMP'/>)"
    R"(<field number='56' name='TargetCompID' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='201' name='OuterField' type='STRING'/>)"
    R"(<field number='202' name='InnerField' type='STRING'/>)"
    R"(<field number='203' name='PlainField' type='STRING'/>)"
    R"(</fields>)"
    R"(<header>)"
    R"(<field name='BeginString' required='Y'/>)"
    R"(<field name='BodyLength' required='Y'/>)"
    R"(<field name='MsgType' required='Y'/>)"
    R"(<field name='SenderCompID' required='Y'/>)"
    R"(<field name='TargetCompID' required='Y'/>)"
    R"(<field name='MsgSeqNum' required='Y'/>)"
    R"(<field name='SendingTime' required='Y'/>)"
    R"(</header>)"
    R"(<trailer><field name='CheckSum' required='Y'/></trailer>)"
    R"(<messages>)"
    R"(<message name='PlainMsg' msgtype='P' msgcat='app'>)"
    R"(<field name='PlainField' required='N'/>)"
    R"(</message>)"
    R"(<message name='GroupMsg' msgtype='G' msgcat='app'>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='OuterField' required='N'/>)"
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerField' required='N'/>)"
    R"(</group></group>)"
    R"(</message>)"
    R"(</messages></fix>)";

// L-050-1 dictionary seam, reproduced without gtest: build a fixpp_dict handle
// over the inline dictionary above and pass it through the REAL setter.
struct DictOwner {
    std::vector<std::byte> arena;
    std::pmr::monotonic_buffer_resource mr;
    fixpp_dict_t* handle = nullptr;

    DictOwner()
        : arena(2UZ * 1024UZ * 1024UZ), mr{arena.data(), arena.size()} {
        auto dict = fixpp::dict::XmlLoader{}.load_from_string(kBenchXml, &mr);
        auto* d = new fixpp_dict{std::make_shared<fixpp::dict::Dictionary>(std::move(dict))};
        handle = reinterpret_cast<fixpp_dict_t*>(d);
    }
    ~DictOwner() { delete reinterpret_cast<fixpp_dict*>(handle); }
    DictOwner(DictOwner const&) = delete;
    DictOwner& operator=(DictOwner const&) = delete;
};

// L-050-5 endpoint seam, reproduced without gtest.
void set_loopback_endpoint(fixpp_session_config_t* cfg) {
    auto* internal = reinterpret_cast<fixpp_session_config*>(cfg);
    internal->cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    internal->cfg.transport_send = [](std::span<const std::byte>) {};
}

fixpp_session_config_t* make_cfg(DictOwner const& dict) {
    fixpp_session_config_t* sc = nullptr;
    fixpp_session_config_create(&sc);
    fixpp_session_config_set_comp_ids(sc, "BSEND", "BTARG");
    fixpp_session_config_set_begin_string(sc, "FIX.4.2");
    fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR);
    fixpp_session_config_set_heartbeat_seconds(sc, 30);
    fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr);
    fixpp_session_config_set_dictionary(sc, dict.handle);
    set_loopback_endpoint(sc);
    return sc;
}

fixpp_engine_config_t* make_engine_cfg() {
    fixpp_engine_config_t* ec = nullptr;
    fixpp_engine_config_create(&ec);
    fixpp_engine_config_set_realtime_clock(ec);
    return ec;
}

// One open session, held for a whole benchmark family so session-open cost
// never leaks into a per-commit row.
struct SessionOwner {
    DictOwner dict;
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = nullptr;

    SessionOwner() {
        fixpp_engine_create(make_engine_cfg(), 1, 0, &eng);
        fixpp_session_open(eng, make_cfg(dict), &sess);
    }
    ~SessionOwner() { fixpp_engine_destroy(eng); }
    SessionOwner(SessionOwner const&) = delete;
    SessionOwner& operator=(SessionOwner const&) = delete;
};

// Builds `35=G` with `outer_instances` outer instances, each optionally
// carrying one nested NoInner instance, and commits. Returns the commit rc.
fixpp_error_t build_and_commit_group(fixpp_session_t* sess, unsigned outer_instances,
                                     bool nested) {
    fixpp_msg_t* msg = nullptr;
    if (fixpp_msg_create_outbound(sess, "G", 1, &msg) != FIXPP_ERR_OK || msg == nullptr) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    fixpp_group_builder_t* outer = nullptr;
    fixpp_msg_group_begin(msg, 100, &outer);
    for (unsigned i = 0; i < outer_instances; ++i) {
        fixpp_entry_t* oe = nullptr;
        fixpp_group_builder_add_entry(outer, &oe);
        fixpp_entry_set_string(oe, 201, "o", 1);
        if (nested) {
            fixpp_group_builder_t* inner = nullptr;
            fixpp_entry_group_begin(oe, 200, &inner);
            fixpp_entry_t* ie = nullptr;
            fixpp_group_builder_add_entry(inner, &ie);
            fixpp_entry_set_string(ie, 202, "i", 1);
            fixpp_msg_group_end(msg, inner);
        }
    }
    fixpp_msg_group_end(msg, outer);

    const uint8_t* out = nullptr;
    std::size_t len = 0;
    fixpp_error_t const rc = fixpp_msg_commit(msg, &out, &len);
    fixpp_msg_destroy(msg);
    return rc;
}

fixpp_error_t build_and_commit_plain(fixpp_session_t* sess) {
    fixpp_msg_t* msg = nullptr;
    if (fixpp_msg_create_outbound(sess, "P", 1, &msg) != FIXPP_ERR_OK || msg == nullptr) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    fixpp_msg_set_string(msg, 203, "p", 1);
    const uint8_t* out = nullptr;
    std::size_t len = 0;
    fixpp_error_t const rc = fixpp_msg_commit(msg, &out, &len);
    fixpp_msg_destroy(msg);
    return rc;
}

// Warm-up + rc check BEFORE the timed loop: a commit that fails closed does
// far LESS work than one that succeeds (it returns at the first violation), so
// an unchecked failing fixture would report a flattering number for a path it
// never walked.
bool warm_up(benchmark::State& state, fixpp_error_t rc, char const* label) {
    if (rc != FIXPP_ERR_OK) {
        state.SkipWithError((std::string(label) + " fixture failed to commit (rc=" +
                             std::to_string(static_cast<int>(rc)) +
                             ") — the timed loop would measure a fail-closed early return")
                                .c_str());
        return false;
    }
    return true;
}

}  // namespace

// ── BM_CapiCommit_NoGroup ───────────────────────────────────────────────────
// Control: no group, so validate_group_grammar descends nothing. This row must
// be flat across 083 — if it moved, the context lookup leaked into the common
// commit path.
static void BM_CapiCommit_NoGroup(benchmark::State& state) {
    SessionOwner s;
    if (!warm_up(state, build_and_commit_plain(s.sess), "NoGroup")) { return; }
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_and_commit_plain(s.sess));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapiCommit_NoGroup);

// ── BM_CapiCommit_Group1 / _Group8 ──────────────────────────────────────────
// One vs eight outer instances of the same shape. The delta between the two
// rows IS the per-instance delimiter-resolution term; neither row alone
// separates it from the fixed per-commit cost.
static void BM_CapiCommit_Group1(benchmark::State& state) {
    SessionOwner s;
    if (!warm_up(state, build_and_commit_group(s.sess, 1, false), "Group1")) { return; }
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_and_commit_group(s.sess, 1, false));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapiCommit_Group1);

static void BM_CapiCommit_Group8(benchmark::State& state) {
    SessionOwner s;
    if (!warm_up(state, build_and_commit_group(s.sess, 8, false), "Group8")) { return; }
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_and_commit_group(s.sess, 8, false));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapiCommit_Group8);

// ── BM_CapiCommit_Nested ────────────────────────────────────────────────────
// Each outer instance carries a nested group, so the check recurses and must
// maintain the parent path (`ctx.pushed(no_tag)`) — the work a root-context-
// only fixture never reaches.
static void BM_CapiCommit_Nested(benchmark::State& state) {
    SessionOwner s;
    if (!warm_up(state, build_and_commit_group(s.sess, 2, true), "Nested")) { return; }
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_and_commit_group(s.sess, 2, true));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapiCommit_Nested);

// ── BM_CapiSessionOpen_TableViewBuild ───────────────────────────────────────
// The ONE config-time `as_table_view()` per session (D-13). Reported as
// SESSION-OPEN cost and deliberately NOT amortized into any per-message row:
// folding it in would hide it, and reporting it per-message would misstate it.
// The inline dictionary here is small, so this row is a FLOOR for a real
// dictionary, not a claim about one.
static void BM_CapiSessionOpen_TableViewBuild(benchmark::State& state) {
    DictOwner dict;
    for (auto _ : state) {
        fixpp_engine_t* eng = nullptr;
        fixpp_engine_create(make_engine_cfg(), 1, 0, &eng);
        fixpp_session_t* sess = nullptr;
        fixpp_session_open(eng, make_cfg(dict), &sess);
        benchmark::DoNotOptimize(sess);
        fixpp_engine_destroy(eng);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapiSessionOpen_TableViewBuild);

BENCHMARK_MAIN();
