// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/scenario_descriptor.hpp
//
// Descriptive interop test-fixture types for specs/016-interop-harness. These
// are not a frozen public API and carry no dependency on production fixpp
// types. Every executed cell MUST carry a non-empty spec_ref (FR-018); a cell
// justified only by reference-engine behavior is invalid.
//
// 018-interop-live-admin: the G1 admin-descriptor extensions (AdminInduction,
// AdminScenarioGroup, AdminScenarioDescriptor) are additive — existing 016
// fields/consumers (Counterparty/Role enums, MatrixCell, etc.) are unchanged.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fixpp::interop {

enum class ScenarioKind : std::uint8_t { happy, thorny, parity };
enum class Counterparty : std::uint8_t { quickfix_cpp, quickfix_j, fix8 };
enum class Role : std::uint8_t { fixpp_initiator, fixpp_acceptor };
enum class Security : std::uint8_t { plain_tcp, tls_logon, mtls_mutual };
enum class RunState : std::uint8_t { pending, running, passed, failed, skipped, known_limitation };

enum class CellDisposition : std::uint8_t {
    live,
    deferred_app_messages,
    deferred_fixt_routing,
    deferred_fix8_revisit,
    deferred_v11_mtls
};

enum class ParityDisposition : std::uint8_t { covered, gap, not_applicable };

enum class CorpusPriority : std::uint8_t {
    p1,
    p2,
    p3,
    watch_p1,
    watch_p2,
    watch_info
};

enum class ProvenanceState : std::uint8_t { closed, open };

struct SeqnumDelta {
    int in = 0;
    int out = 0;
};

struct PassCriteria {
    std::string fixpp_end_state;
    std::string counterparty_terminal;
    SeqnumDelta seqnum_delta;
    bool golden_match = false;
};

struct Scenario {
    std::string id;
    ScenarioKind kind = ScenarioKind::happy;
    std::string preconditions;
    std::string driven_sequence;
    PassCriteria pass_criteria;
    std::chrono::milliseconds deadline{0};
    std::optional<std::string> skip_reason;
    std::string spec_ref;
    RunState state = RunState::pending;
};

struct MatrixCell : Scenario {
    Counterparty counterparty = Counterparty::quickfix_cpp;
    Role role = Role::fixpp_initiator;
    std::string fix_version = "FIX.4.4";
    Security security = Security::plain_tcp;
    std::string event_chain;
    std::string business = "none";
    std::string golden_ref;
    CellDisposition disposition = CellDisposition::live;
    std::optional<int> reconnect_max_attempts;
    std::optional<std::chrono::milliseconds> stop_watchdog;
};

struct Provenance {
    std::string engine;
    std::string ref;
    std::string url;
    ProvenanceState pstate = ProvenanceState::closed;
};

struct CorpusScenario : Scenario {
    Provenance provenance;
    std::string category;
    CorpusPriority priority = CorpusPriority::p1;
    bool differentiator = false;
    std::optional<std::string> known_limitation;
};

struct ParityRow {
    std::string source;
    std::string behavior;
    ParityDisposition disposition = ParityDisposition::gap;
    std::string citation;
};

inline bool has_spec_ref(const Scenario& s)
{
    return !s.spec_ref.empty();
}

// ---------------------------------------------------------------------------
// 018-interop-live-admin G1 admin-descriptor extensions
//
// These types are additive; 016 consumers remain unaffected.
// Anchored to: specs/018-interop-live-admin/contracts/admin-scenario-descriptor.md
//   rule 8 (induction values per scenario_group) and rule 7 (AC exact-set).
// ---------------------------------------------------------------------------

// The five G1 scenario groups (admin-scenario-descriptor.md rule 8 table).
enum class AdminScenarioGroup : std::uint8_t {
    testrequest_echo,
    idle_cadence,
    recovery_inbound,
    recovery_outbound,
    session_reject,
};

// The induction mechanism for a cell — value is fixed BY scenario_group
// (rule 8 table): each group maps to exactly one mechanism; a cell MUST NOT
// mix mechanisms (different mechanisms yield different golden frame sets).
//   testrequest_echo  -> inbound_silence   (fixpp emits TestRequest due to HB timeout)
//   idle_cadence      -> idle_observation  (passive observation; no active induction)
//   recovery_inbound  -> withhold_frame    (parent withholds a frame to induce gap)
//   recovery_outbound -> qfj_restart_resend (QFJ restart/reconnect at lower seqnum)
//   session_reject    -> proxy_corrupt     (parent corrupts a frame in-flight)
enum class AdminInduction : std::uint8_t {
    inbound_silence,    // testrequest_echo
    idle_observation,   // idle_cadence
    withhold_frame,     // recovery_inbound
    qfj_restart_resend, // recovery_outbound
    proxy_corrupt,      // session_reject
};

// One admin round-trip's acceptance coverage reference (e.g. "US1-1").
// An AdminScenarioDescriptor carries the union of its round_trips' ac_ref
// values as acceptance_ids. Per rule 7, the set must equal the expected
// AC subset for the (scenario_group, role) pair (exact-set equality, not
// subset — feedback_completeness_gate_exact_set_not_subset).
struct AdminRoundTripRef {
    std::string ac_ref;   // e.g. "US1-1", "US3-2"
    std::string spec_ref; // "[FIX-SL §...]"
};

// Per-cell G1 admin descriptor — carries the fields required by the
// admin-scenario-descriptor contract (rules 1-8).
//
// Intended use: cells declare a static AdminScenarioDescriptor and a
// validation helper asserts that induction == expected_induction(group)
// and acceptance_ids == union(round_trips[].ac_ref) == expected_ac_ids(group,role).
struct AdminScenarioDescriptor {
    std::string cell_id;                           // e.g. "HP-QFj-init-fix44-testreq-echo"
    AdminScenarioGroup scenario_group = AdminScenarioGroup::testrequest_echo;
    Role role = Role::fixpp_initiator;             // both required per FR-005a / rule 1
    Counterparty counterparty = Counterparty::quickfix_j;
    std::string spec_ref;                          // "[FIX-SL §...]" mandatory
    std::string golden_ref;                        // "happy/golden/<cell_id>.fix"
    std::vector<AdminRoundTripRef> round_trips;    // >=1; each carries ac_ref (E2)
    std::set<std::string> acceptance_ids;          // rule 7 exact-set target
    AdminInduction induction = AdminInduction::inbound_silence; // rule 8: fixed by group
    std::chrono::milliseconds self_deadline_ms{0}; // FR-010 per-cell bound
};

// Returns the expected induction mechanism for a given scenario_group (rule 8 table).
inline AdminInduction expected_induction(AdminScenarioGroup group)
{
    switch (group) {
        case AdminScenarioGroup::testrequest_echo:  return AdminInduction::inbound_silence;
        case AdminScenarioGroup::idle_cadence:      return AdminInduction::idle_observation;
        case AdminScenarioGroup::recovery_inbound:  return AdminInduction::withhold_frame;
        case AdminScenarioGroup::recovery_outbound: return AdminInduction::qfj_restart_resend;
        case AdminScenarioGroup::session_reject:    return AdminInduction::proxy_corrupt;
    }
    return AdminInduction::inbound_silence; // unreachable
}

// Returns the expected acceptance_ids for a (scenario_group, role) pair (rule 8 table).
// Both roles are required to independently cover every AC of their group (rule 7).
inline std::set<std::string> expected_ac_ids(AdminScenarioGroup group, Role /*role*/)
{
    // Per rule 7: each role independently covers every AC of its group.
    // The expected set is the SAME for both roles within a group.
    switch (group) {
        case AdminScenarioGroup::testrequest_echo:  return {"US1-1", "US1-2", "US1-3"};
        case AdminScenarioGroup::idle_cadence:      return {"US2-1", "US2-2"};
        case AdminScenarioGroup::recovery_inbound:  return {"US3-1", "US3-2", "US3-4"};
        case AdminScenarioGroup::recovery_outbound: return {"US3-3", "US3-4"};
        case AdminScenarioGroup::session_reject:    return {"US4-1", "US4-2"};
    }
    return {};
}

// Validates the descriptor's induction and acceptance_ids against the rule 8 table.
// Returns an empty string on success; a human-readable error string on failure.
// Cell drivers call this at fixture setup (rule 7 + rule 8 enforcement).
inline std::string validate_admin_descriptor(const AdminScenarioDescriptor& d)
{
    // Rule 8: induction must match the group's required mechanism.
    const AdminInduction exp_ind = expected_induction(d.scenario_group);
    if (d.induction != exp_ind) {
        return "induction mismatch for cell " + d.cell_id;
    }

    // Rule 7: acceptance_ids must equal the union of round_trips[].ac_ref.
    std::set<std::string> union_refs;
    for (const auto& rt : d.round_trips) {
        union_refs.insert(rt.ac_ref);
    }
    if (union_refs != d.acceptance_ids) {
        return "acceptance_ids != union(round_trips[].ac_ref) for cell " + d.cell_id;
    }

    // Rule 7: acceptance_ids must equal the expected AC subset for the group.
    const std::set<std::string> exp_ids = expected_ac_ids(d.scenario_group, d.role);
    if (d.acceptance_ids != exp_ids) {
        return "acceptance_ids != expected_ac_ids for cell " + d.cell_id;
    }

    // T029 identity invariant: golden_ref basename must equal cell_id.
    // The actual golden is loaded via admin_golden_path(cell_id), not from this
    // field; this check makes the recorded field consistency-checked.
    if (d.golden_ref != "happy/golden/" + d.cell_id + ".fix") {
        return "golden_ref basename != cell_id for cell " + d.cell_id;
    }

    // FR-010 / rule 4: per-cell self-deadline must be positive.
    if (d.self_deadline_ms <= std::chrono::milliseconds{0}) {
        return "self_deadline_ms must be > 0 for cell " + d.cell_id;
    }

    return {};
}

}  // namespace fixpp::interop
